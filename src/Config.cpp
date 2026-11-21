// SPDX-License-Identifier: GPL-3.0-or-later
#include "Config.h"
#include <Preferences.h>
static bool pinOk(int p) { return (p >= 1 && p <= 18) || p == 21 || (p >= 38 && p <= 42) || p == 47 || p == 48; }
static bool identifier(const String& s, bool topic) {
    if (s.isEmpty() || s.length() > 64) return false;
    for (char c : s) if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || (topic && c == '/'))) return false;
    return !s.startsWith("/") && !s.endsWith("/");
}
void Config::toJson(JsonDocument& d, bool secrets) const {
    d["ssid"] = ssid; d["broker"] = broker; d["mqttUser"] = mqttUser; d["base"] = base; d["hostname"] = hostname;
    d["port"] = port; d["publishSeconds"] = publishSeconds; d["rx"] = rx; d["tx"] = tx; d["minutes"] = minutes;
    d["protocol"] = protocol;
    d["discovery"] = discovery; d["json"] = json;
    d["adminUser"] = adminUser; d["adminPasswordSet"] = !adminPassword.isEmpty();
    if (secrets) { d["wifiPassword"] = wifiPassword; d["mqttPassword"] = mqttPassword; d["adminPassword"] = adminPassword; d["webAuthVersion"] = 1; }
}
bool Config::update(JsonVariantConst d, String& error) {
    if (!d.is<JsonObjectConst>()) { error = "JSON object required"; return false; }
    Config next = *this;
    const char* strings[] = {"ssid", "wifiPassword", "broker", "mqttUser", "mqttPassword", "adminUser", "adminPassword", "base", "hostname", "protocol"};
    String* targets[] = {&next.ssid,&next.wifiPassword,&next.broker,&next.mqttUser,&next.mqttPassword,&next.adminUser,&next.adminPassword,&next.base,&next.hostname,&next.protocol};
    for (size_t i = 0; i < 10; ++i) if (!d[strings[i]].isNull()) {
        if (!d[strings[i]].is<const char*>()) { error = "invalid string"; return false; }
        *targets[i] = d[strings[i]].as<String>();
    }
    struct Bound { const char* key; int low; int high; };
    for (auto b : {Bound{"port",1,65535}, Bound{"publishSeconds",2,3600}, Bound{"rx",1,48}, Bound{"tx",1,48}, Bound{"minutes",1,120}}) {
        if (!d[b.key].isNull() && (!d[b.key].is<int>() || d[b.key].as<int>() < b.low || d[b.key].as<int>() > b.high)) {
            error = String("out of range: ") + b.key; return false;
        }
    }
    if (!d["port"].isNull()) next.port = d["port"].as<int>();
    if (!d["publishSeconds"].isNull()) next.publishSeconds = d["publishSeconds"].as<int>();
    if (!d["rx"].isNull()) next.rx = d["rx"].as<int>();
    if (!d["tx"].isNull()) next.tx = d["tx"].as<int>();
    if (!d["minutes"].isNull()) next.minutes = d["minutes"].as<int>();
    for (const char* key : {"discovery", "json"}) if (!d[key].isNull() && !d[key].is<bool>()) { error = "boolean required"; return false; }
    if (!d["discovery"].isNull()) next.discovery = d["discovery"].as<bool>();
    if (!d["json"].isNull()) next.json = d["json"].as<bool>();
    if (!pinOk(next.rx) || !pinOk(next.tx) || next.rx == next.tx) { error = "GPIO unavailable/reserved (USB 19/20, flash, straps)"; return false; }
    if (next.ssid.length() > 32 || next.wifiPassword.length() > 63 || (!next.wifiPassword.isEmpty() && next.wifiPassword.length() < 8) ||
        next.broker.length() > 128 || next.mqttUser.length() > 128 || next.mqttPassword.length() > 128 ||
        next.adminUser.length() > 63 || next.adminUser.indexOf(':') >= 0 || next.adminPassword.length() > 63 || !identifier(next.base, true) || !identifier(next.hostname, false) ||
        !(next.protocol == "auto" || next.protocol == "wbus" || next.protocol == "ttckwp25" || next.protocol == "ttckwp30" || next.protocol == "ttcwtt" || next.protocol == "fiat" || next.protocol == "psa" || next.protocol == "bmw")) {
        error = "invalid length, password or topic/hostname"; return false;
    }
    *this = next; return true;
}
bool Config::save() const {
    JsonDocument d; toJson(d, true); String s; serializeJson(d, s);
    Preferences p; if (!p.begin("wtt2mqtt", false)) return false;
    bool ok = p.putString("config", s) == s.length(); p.end(); return ok;
}
void Config::begin() {
    Preferences p; p.begin("wtt2mqtt", true); String raw = p.getString("config", ""); p.end();
    if (!raw.isEmpty()) {
        JsonDocument d; String e;
        if (!deserializeJson(d, raw)) {
            // Older builds imposed a generated admin password. Disable that legacy
            // login once without discarding WLAN, MQTT or other saved settings.
            bool legacy = d["webAuthVersion"].as<int>() != 1;
            if (legacy) { d["adminUser"] = ""; d["adminPassword"] = ""; }
            if (update(d.as<JsonVariantConst>(), e)) { if (legacy) save(); return; }
        }
    }
    save();
}
