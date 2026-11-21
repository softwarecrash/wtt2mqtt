// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
struct Config {
    String ssid, wifiPassword, broker, mqttUser, mqttPassword, adminUser, adminPassword;
    String base = "wtt2mqtt", hostname = "wtt2mqtt";
    // "auto" probes every supported profile; other values map to ProtocolScan profiles.
    String protocol = "auto";
    uint16_t port = 1883, publishSeconds = 10;
    uint8_t rx = 16, tx = 17, minutes = 30;
    bool discovery = true, json = true;
    bool webAuthEnabled() const { return !adminUser.isEmpty() && !adminPassword.isEmpty(); }
    void begin();
    void toJson(JsonDocument& doc, bool secrets = false) const;
    bool update(JsonVariantConst doc, String& error);
    bool save() const;
};
