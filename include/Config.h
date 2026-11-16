// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
struct Config {
    String ssid, wifiPassword, broker, mqttUser, mqttPassword, adminPassword;
    String base = "wtt2mqtt", hostname = "wtt2mqtt";
    uint16_t port = 1883, publishSeconds = 10;
    uint8_t rx = 16, tx = 17, minutes = 30;
    bool discovery = true, json = true;
    void begin();
    void toJson(JsonDocument& doc, bool secrets = false) const;
    bool update(JsonVariantConst doc, String& error);
    bool save() const;
};
