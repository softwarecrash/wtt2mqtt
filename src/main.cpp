// SPDX-License-Identifier: GPL-3.0-or-later
#include <Arduino.h>
#include "Config.h"
#include "Network.h"
#include "Runtime.h"
#include <Heater.h>
#include "RxTiming.h"
#include <ProtocolScan.h>
#include <driver/gpio.h>
static Config config;
class UartPort final: public wtt::Port {
    HardwareSerial serial_{1}; bool attached_ = false;
    std::atomic<uint32_t> errors_[6]{};
    portMUX_TYPE timingMux_ = portMUX_INITIALIZER_UNLOCKED;
    RxTiming timing_;
    uint8_t rxPin_ = 16;
    bool timingActive_ = false;
    bool wakeTiming_ = false, wakeInitialHigh_ = true;
    uint32_t wakeAt_ = 0;
    static void ARDUINO_ISR_ATTR rxChanged(void* context) {
        auto* self = static_cast<UartPort*>(context);
        uint32_t now = micros(); bool high = digitalRead(self->rxPin_);
        portENTER_CRITICAL_ISR(&self->timingMux_);
        if (self->timingActive_) self->timing_.edge(now,high);
        bool limit = self->timing_.edges >= RxTiming::Limit;
        portEXIT_CRITICAL_ISR(&self->timingMux_);
        if (limit) gpio_intr_disable(static_cast<gpio_num_t>(self->rxPin_));
    }
public:
    void drive(bool high) override {
        if (attached_) { gpio_intr_disable(static_cast<gpio_num_t>(rxPin_)); detachInterrupt(rxPin_); serial_.end(); attached_ = false; }
        // RXD is open-drain. Keep a defined idle level while UART is detached.
        rxPin_=config.rx; pinMode(rxPin_,INPUT_PULLUP);
        pinMode(config.tx,OUTPUT); digitalWrite(config.tx,high ? HIGH : LOW);
    }
    void uart() override {
        configure(2400,true);
    }
    bool configure(uint32_t baud, bool even) override {
        if (attached_) { gpio_intr_disable(static_cast<gpio_num_t>(rxPin_)); detachInterrupt(rxPin_); serial_.end(); }
        serial_.begin(baud,even ? SERIAL_8E1 : SERIAL_8N1,config.rx,config.tx); attached_ = true;
        rxPin_ = config.rx;
        // Apply after begin(): UART pin setup may reset the GPIO pull mode.
        // Do not use pinMode here, which could change the UART pin routing.
        gpio_set_pull_mode(static_cast<gpio_num_t>(rxPin_),GPIO_PULLUP_ONLY);
        attachInterruptArg(rxPin_,rxChanged,this,CHANGE);
        gpio_intr_disable(static_cast<gpio_num_t>(rxPin_));
        // HardwareSerial::end() clears this callback, so reattach after each break.
        // The driver task only increments counters; USB/logging stay in our loop.
        serial_.onReceiveError([this](hardwareSerial_error_t error) {
            unsigned index = static_cast<unsigned>(error);
            if (index > 0 && index < 6) errors_[index].fetch_add(1,std::memory_order_relaxed);
        });
        return true;
    }
    int read() override { return attached_ && serial_.available() ? serial_.read() : -1; }
    void timing(const char* label,bool start) override {
        if(start) {
            rxPin_=config.rx;
            if(!attached_) { pinMode(rxPin_,INPUT_PULLUP); attachInterruptArg(rxPin_,rxChanged,this,CHANGE); }
            gpio_intr_disable(static_cast<gpio_num_t>(rxPin_));
            portENTER_CRITICAL(&timingMux_);
            wakeAt_=micros(); wakeInitialHigh_=digitalRead(rxPin_);
            timing_.start(wakeAt_,wakeInitialHigh_); timingActive_=wakeTiming_=true;
            portEXIT_CRITICAL(&timingMux_);
            gpio_intr_enable(static_cast<gpio_num_t>(rxPin_));
        } else if(wakeTiming_) {
            gpio_intr_disable(static_cast<gpio_num_t>(rxPin_));
            portENTER_CRITICAL(&timingMux_);
            timingActive_=false; RxTiming captured=timing_; wakeTiming_=false;
            portEXIT_CRITICAL(&timingMux_);
            uint32_t end=micros(); captured.finish(end);
            char samples[145]{}; size_t pos=0;
            for(unsigned i=0;i<captured.stored && pos+13<sizeof samples;++i)
                pos+=snprintf(samples+pos,sizeof(samples)-pos,"%c%lu ",captured.sampleHigh[i]?'H':'L',(unsigned long)captured.duration[i]);
            logLine("%s us=%lu RX=%s->%s edges=%u low_max_us=%lu samples_us=%s%s",label,(unsigned long)(end-wakeAt_),wakeInitialHigh_?"H":"L",digitalRead(rxPin_)?"H":"L",captured.edges,(unsigned long)captured.longestLow,samples,captured.edges>=RxTiming::Limit?" [limit]":"");
        }
    }
    void write(const uint8_t* b,size_t n) override {
        wakeTiming_=false;
        portENTER_CRITICAL(&timingMux_);
        timing_.start(micros(),digitalRead(rxPin_)); timingActive_ = true;
        portEXIT_CRITICAL(&timingMux_);
        gpio_intr_enable(static_cast<gpio_num_t>(rxPin_));
        serial_.write(b,n);
    }
    void writePart(const uint8_t* b,size_t n) override { serial_.write(b,n); }
    void reportErrors() {
        gpio_intr_disable(static_cast<gpio_num_t>(rxPin_));
        portENTER_CRITICAL(&timingMux_);
        timingActive_ = false; RxTiming timing = timing_;
        portEXIT_CRITICAL(&timingMux_);
        timing.finish(micros());
        uint32_t counts[6]{};
        for (unsigned i=1; i<6; ++i) counts[i]=errors_[i].exchange(0,std::memory_order_relaxed);
        logLine("W-Bus UART since last result: parity=%lu frame=%lu break=%lu fifo=%lu buffer=%lu RX=%s",
                (unsigned long)counts[UART_PARITY_ERROR],(unsigned long)counts[UART_FRAME_ERROR],
                (unsigned long)counts[UART_BREAK_ERROR],(unsigned long)counts[UART_FIFO_OVF_ERROR],
                (unsigned long)counts[UART_BUFFER_FULL_ERROR],digitalRead(config.rx) ? "HIGH":"LOW");
        char samples[145]{}; size_t pos=0;
        for (unsigned i=0; i<timing.stored && pos+13<sizeof samples; ++i)
            pos+=snprintf(samples+pos,sizeof(samples)-pos,"%c%lu ",timing.sampleHigh[i] ? 'H':'L',(unsigned long)timing.duration[i]);
        logLine("W-Bus RX_TIMING edges=%u low_max_us=%lu samples_us=%s%s",timing.edges,(unsigned long)timing.longestLow,
                samples,timing.edges>=RxTiming::Limit ? " [limit]":"");
    }
};
static UartPort port;
static wtt::Bus bus(port);
static wtt::Heater heater(bus);
static wtt::ProtocolScan scanner(port);
static bool scanPending = false;
static void trace(const char* direction,const uint8_t* b,size_t n) {
    char text[193]; size_t k = 0;
    for (size_t i = 0; i < n && k + 3 < sizeof text; ++i) k += snprintf(text+k,sizeof(text)-k,"%02X ",b[i]);
    text[k] = 0; logLine("W-Bus %s %s",direction,text);
}
static void busResult(const wtt::Result& result) {
    logLine("W-Bus RESULT cmd=%02X %s echo=%u/%u received=%u elapsed=%u ms",result.cmd,wtt::errorText(result.error),
            result.echoLength,result.txLength,result.wireLength,result.durationMs);
    if (result.error == wtt::Error::EchoMismatch)
        logLine("W-Bus ECHO mismatch index=%u expected=%02X received=%02X",result.echoIndex,result.expectedEcho,result.actualEcho);
    port.reportErrors();
}
static void scanResult(unsigned profile,unsigned result) {
    logLine("SCAN profile=%u %s result=%s",profile+1,wtt::ProtocolScan::name(profile),wtt::ProtocolScan::outcome(result));
    port.reportErrors();
}
static void scanAttempt(unsigned profile,uint8_t service,unsigned result,size_t received,bool echo) {
    logLine("SCAN attempt profile=%u service=%02X result=%s bytes=%u echo=%s",profile+1,service,wtt::ProtocolScan::outcome(result),unsigned(received),echo?"complete":"missing/mismatch");
}
static void networkTask(void*) {
    static_assert(configUSE_PREEMPTION == 1 && configUSE_TIME_SLICING == 1,
                  "Network worker requires preemptive time slicing with IDLE0");
    Network network(config); network.begin();
    // Always block for at least one tick, even if pdMS_TO_TICKS(2) rounds to zero.
    const TickType_t pause = pdMS_TO_TICKS(2) > 0 ? pdMS_TO_TICKS(2) : 1;
    for (;;) { network.tick(); vTaskDelay(pause); }
}
void setup() {
    Serial.begin(115200); Serial.setTxTimeoutMs(0); config.begin();
    logLine("WTT2MQTT 0.1.0; 4 MB dual OTA; no filesystem; no PSRAM");
    // Credentials are deliberately not written to the web-visible ring log; request over USB.
    commandQueue = xQueueCreate(8,sizeof(Command));
    if (!commandQueue) { logLine("FATAL command queue allocation failed"); return; }
    bus.trace = trace; bus.completed = busResult; heater.begin(millis()); storeSnapshot(heater.snapshot());
    scanner.trace = trace; scanner.report = scanResult; scanner.attempt = scanAttempt;
    logLine("Protocol scan available: W-Bus + legacy TT-C 10400/8N1; build %s %s",__DATE__,__TIME__);
    logLine("W-Bus UART: RX=%u TX=%u, 2400 baud 8E1; RX internal pull-up enabled; UART errors + RX_TIMING enabled",config.rx,config.tx);
    // Arduino 2.0.17 Stream::timedRead() can busy-wait inside WebServer parsing.
    // A higher-priority worker would starve IDLE0 during partial/slow requests.
    // Share priority with IDLE0 so preemptive time slicing keeps its WDT feed alive,
    // including inside handleClient(), before the explicit delay above is reached.
    logLine("Network worker: idle priority (HTTP watchdog fix)");
    if (xTaskCreatePinnedToCore(networkTask,"network",12288,nullptr,tskIDLE_PRIORITY,nullptr,0) != pdPASS) logLine("FATAL network task allocation failed");
}
void loop() {
    uint32_t now = millis(); Command c;
    const bool serviceRequested = otaActive.load();
    if (commandQueue && xQueueReceive(commandQueue,&c,0) == pdTRUE) {
        bool allowed = !(otaActive.load() && c.action != wtt::Action::Stop);
        bool ok = false;
        if(c.action==wtt::Action::Scan) {
            ok=allowed && heater.restartSafe() && !scanPending && !scanner.active();
            if(ok) ok=scanRecording(true);
            if(ok) { scanPending=true; logLine("SCAN queued; normal polling paused; wakeup RX capture + legacy fault read enabled"); }
        } else if(c.action==wtt::Action::CancelScan || c.action==wtt::Action::Stop) {
            scanPending=false; scanner.cancel();
            ok=allowed && (c.action==wtt::Action::CancelScan || heater.command(c.action,c.minutes,now));
        } else if(!scanPending && !scanner.active()) ok=allowed && heater.command(c.action,c.minutes,now);
        logLine("Command %u %s",unsigned(c.action),ok ? "accepted" : "rejected");
    }
    if(serviceRequested) { scanPending=false; scanner.cancel(); }
    if(!scanner.active()) heater.tick(now,!scanPending);
    if(scanPending && bus.idle()) { scanPending=false; heater.invalidateCommunication(); scanner.start(now); }
    scanner.tick(now);
    if(scanRecordingActive() && !scanPending && !scanner.active()) {
        logLine("SCAN ended; 7 profiles including Fiat/PSA/BMW; UART restored 2400/8E1. Rover not tested. Transcript retained until next scan/reboot.");
        scanRecording(false);
    }
    auto snapshot=heater.snapshot(); snapshot.scanActive=scanPending || scanner.active(); snapshot.scanProfile=scanner.profile();
    memcpy(snapshot.scanResults,scanner.results(),sizeof snapshot.scanResults);
    // Probe responses never become normal W-Bus telemetry or enable starts.
    if(snapshot.scanActive) { snapshot.online=false; snapshot.sensorsValid=false; }
    storeSnapshot(snapshot); canUpdate = heater.updateSafe(now);
    serviceReady = serviceRequested && heater.updateSafe(now);
    restartReady = serviceRequested && heater.restartSafe();
    pumpUsbLog();
    // Bounded, local USB commands. Never accept arbitrary bus writes.
    static char line[40]; static size_t pos = 0;
    for (int budget = 0; budget < 40 && Serial.available(); ++budget) {
        char ch = char(Serial.read());
        if (ch == '\n' || ch == '\r') {
            line[pos] = 0;
            if (!strcmp(line,"credentials")) {
                char credentials[170];
                if (config.webAuthEnabled()) snprintf(credentials,sizeof credentials,"HTTP user: %s / password: %s",config.adminUser.c_str(),config.adminPassword.c_str());
                else strlcpy(credentials,"HTTP authentication disabled",sizeof credentials);
                usbLine(credentials);
            } else if (!strcmp(line,"stop")) enqueue(wtt::Action::Stop);
            else if (!strcmp(line,"scan")) enqueue(wtt::Action::Scan);
            else if (!strcmp(line,"scan-stop")) enqueue(wtt::Action::CancelScan);
            else if (!strcmp(line,"diagnose")) enqueue(wtt::Action::Diagnose);
            else if (!strcmp(line,"status")) logLine("status: %s, online=%u, error=%s",heater.snapshot().phase,heater.snapshot().online,wtt::errorText(heater.snapshot().error));
            pos = 0;
        } else if (ch >= 32 && pos < sizeof(line)-1) line[pos++] = ch;
    }
    // Yield to the scheduler; no protocol/network wait in the control loop.
    vTaskDelay(1);
}
