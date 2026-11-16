// SPDX-License-Identifier: GPL-3.0-or-later
#include <Arduino.h>
#include "Config.h"
#include "Network.h"
#include "Runtime.h"
#include <Heater.h>
#include "RxTiming.h"
#include <driver/gpio.h>
static Config config;
class UartPort final: public wtt::Port {
    HardwareSerial serial_{1}; bool attached_ = false;
    std::atomic<uint32_t> errors_[6]{};
    portMUX_TYPE timingMux_ = portMUX_INITIALIZER_UNLOCKED;
    RxTiming timing_;
    uint8_t rxPin_ = 16;
    bool timingActive_ = false;
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
        pinMode(config.tx,OUTPUT); digitalWrite(config.tx,high ? HIGH : LOW);
    }
    void uart() override {
        serial_.begin(2400,SERIAL_8E1,config.rx,config.tx); attached_ = true;
        rxPin_ = config.rx;
        attachInterruptArg(rxPin_,rxChanged,this,CHANGE);
        gpio_intr_disable(static_cast<gpio_num_t>(rxPin_));
        // HardwareSerial::end() clears this callback, so reattach after each break.
        // The driver task only increments counters; USB/logging stay in our loop.
        serial_.onReceiveError([this](hardwareSerial_error_t error) {
            unsigned index = static_cast<unsigned>(error);
            if (index > 0 && index < 6) errors_[index].fetch_add(1,std::memory_order_relaxed);
        });
    }
    int read() override { return attached_ && serial_.available() ? serial_.read() : -1; }
    void write(const uint8_t* b,size_t n) override {
        portENTER_CRITICAL(&timingMux_);
        timing_.start(micros(),digitalRead(rxPin_)); timingActive_ = true;
        portEXIT_CRITICAL(&timingMux_);
        gpio_intr_enable(static_cast<gpio_num_t>(rxPin_));
        serial_.write(b,n);
    }
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
    logLine("W-Bus UART: RX=%u TX=%u, 2400 baud 8E1; UART errors + RX_TIMING enabled",config.rx,config.tx);
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
        bool ok = allowed && heater.command(c.action,c.minutes,now);
        logLine("Command %u %s",unsigned(c.action),ok ? "accepted" : "rejected");
    }
    heater.tick(now); storeSnapshot(heater.snapshot()); canUpdate = heater.updateSafe(now);
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
            else if (!strcmp(line,"diagnose")) enqueue(wtt::Action::Diagnose);
            else if (!strcmp(line,"status")) logLine("status: %s, online=%u, error=%s",heater.snapshot().phase,heater.snapshot().online,wtt::errorText(heater.snapshot().error));
            pos = 0;
        } else if (ch >= 32 && pos < sizeof(line)-1) line[pos++] = ch;
    }
    // Yield to the scheduler; no protocol/network wait in the control loop.
    vTaskDelay(1);
}
