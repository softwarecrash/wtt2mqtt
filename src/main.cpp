// SPDX-License-Identifier: GPL-3.0-or-later
#include <Arduino.h>
#include "Config.h"
#include "Network.h"
#include "Runtime.h"
#include <Heater.h>
static Config config;
class UartPort final: public wtt::Port {
    HardwareSerial serial_{1}; bool attached_ = false;
public:
    void drive(bool high) override {
        if (attached_) { serial_.end(); attached_ = false; }
        pinMode(config.tx,OUTPUT); digitalWrite(config.tx,high ? HIGH : LOW);
    }
    void uart() override { serial_.begin(2400,SERIAL_8E1,config.rx,config.tx); attached_ = true; }
    int read() override { return attached_ && serial_.available() ? serial_.read() : -1; }
    void write(const uint8_t* b,size_t n) override { serial_.write(b,n); }
};
static UartPort port;
static wtt::Bus bus(port);
static wtt::Heater heater(bus);
static void trace(const char* direction,const uint8_t* b,size_t n) {
    char text[193]; size_t k = 0;
    for (size_t i = 0; i < n && k + 3 < sizeof text; ++i) k += snprintf(text+k,sizeof(text)-k,"%02X ",b[i]);
    text[k] = 0; logLine("W-Bus %s %s",direction,text);
}
static void networkTask(void*) {
    Network network(config); network.begin();
    for (;;) { network.tick(); vTaskDelay(pdMS_TO_TICKS(2)); }
}
void setup() {
    Serial.begin(115200); Serial.setTxTimeoutMs(0); config.begin();
    logLine("WTT2MQTT 0.1.0; 4 MB dual OTA; no filesystem; no PSRAM");
    // Credentials are deliberately not written to the web-visible ring log; request over USB.
    commandQueue = xQueueCreate(8,sizeof(Command));
    if (!commandQueue) { logLine("FATAL command queue allocation failed"); return; }
    bus.trace = trace; heater.begin(millis()); storeSnapshot(heater.snapshot());
    if (xTaskCreatePinnedToCore(networkTask,"network",12288,nullptr,1,nullptr,0) != pdPASS) logLine("FATAL network task allocation failed");
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
                char credentials[100]; snprintf(credentials,sizeof credentials,"admin / AP password: %s",config.adminPassword.c_str()); usbLine(credentials);
            } else if (!strcmp(line,"stop")) enqueue(wtt::Action::Stop);
            else if (!strcmp(line,"diagnose")) enqueue(wtt::Action::Diagnose);
            else if (!strcmp(line,"status")) logLine("status: %s, online=%u, error=%s",heater.snapshot().phase,heater.snapshot().online,wtt::errorText(heater.snapshot().error));
            pos = 0;
        } else if (ch >= 32 && pos < sizeof(line)-1) line[pos++] = ch;
    }
    // Yield to the scheduler; no protocol/network wait in the control loop.
    vTaskDelay(1);
}
