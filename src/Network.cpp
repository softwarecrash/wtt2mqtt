// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network.h"
#include "Runtime.h"
#include "WebAssets.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
void Network::startAP() {
    if (ap_) return;
    WiFi.mode(WIFI_AP_STA); WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    ap_ = WiFi.softAP(apName_.c_str(), cfg_.adminPassword.c_str());
    if (ap_) dns_.start(53, "*", IPAddress(192,168,4,1));
    logLine("Setup AP %s at 192.168.4.1; USB command: credentials", apName_.c_str());
}
void Network::begin() {
    char ident[32]; snprintf(ident, sizeof ident, "wtt2mqtt-%012llx", ESP.getEfuseMac());
    id_ = ident; apName_ = "WTT2MQTT-" + id_.substring(id_.length() - 6);
    WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setHostname(cfg_.hostname.c_str()); WiFi.setSleep(false); WiFi.setAutoReconnect(true);
    if (!cfg_.ssid.isEmpty()) WiFi.begin(cfg_.ssid.c_str(), cfg_.wifiPassword.c_str()); else startAP();
    reconnectAt_ = millis();
    if (MDNS.begin(cfg_.hostname.c_str())) MDNS.addService("http", "tcp", 80);
    routes(); server_.begin(); mqttBegin();
}
bool Network::auth(bool mutation) {
    if (!server_.authenticate("admin", cfg_.adminPassword.c_str())) { server_.requestAuthentication(); return false; }
    if (mutation && server_.header("X-WTT2MQTT") != "1") { server_.send(403, "text/plain", "X-WTT2MQTT header required"); return false; }
    return true;
}
void Network::sendJson(JsonDocument& doc, int status) {
    String s; serializeJson(doc, s); server_.sendHeader("Cache-Control", "no-store"); server_.send(status, "application/json", s);
}
void Network::routes() {
    const char* headers[] = {"X-WTT2MQTT"}; server_.collectHeaders(headers, 1);
    server_.on("/api/status", HTTP_GET, [this] {
        if (!auth()) return; JsonDocument d; stateJson(d);
        d["wifi_connected"] = WiFi.status() == WL_CONNECTED; d["mqtt_connected"] = connected_.load();
        d["ap"] = ap_; d["ip"] = (WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP()).toString();
        d["rssi"] = WiFi.RSSI(); sendJson(d);
    });
    server_.on("/api/config", HTTP_GET, [this] { if (!auth()) return; JsonDocument d; pendingConfig_.toJson(d); sendJson(d); });
    server_.on("/api/config", HTTP_POST, [this] {
        if (!auth(true)) return;
        if (otaActive.load()) { server_.send(409,"text/plain","Service operation in progress"); return; }
        if (server_.arg("plain").length() > 2048) { server_.send(413,"text/plain","Request too large"); return; }
        JsonDocument d; auto parseError = deserializeJson(d, server_.arg("plain")); Config next = pendingConfig_; String error;
        if (parseError || !next.update(d.as<JsonVariantConst>(), error)) { server_.send(400,"text/plain",parseError ? "Invalid JSON" : error); return; }
        if (!next.save()) { server_.send(500,"text/plain","NVS write failed"); return; }
        pendingConfig_ = next;
        server_.send(200,"application/json","{\"saved\":true,\"restart_required\":true}");
        logLine("Configuration saved; restart required");
    });
    server_.on("/api/command", HTTP_POST, [this] {
        if (!auth(true)) return;
        JsonDocument d;
        if (server_.arg("plain").length() > 128 || deserializeJson(d, server_.arg("plain"))) { server_.send(400,"text/plain","Invalid command JSON"); return; }
        String a = d["action"] | ""; wtt::Action action;
        if (a == "start") action = wtt::Action::Start; else if (a == "stop") action = wtt::Action::Stop;
        else if (a == "diagnose") action = wtt::Action::Diagnose;
        else { server_.send(400,"text/plain","Allowed: start, stop, diagnose"); return; }
        int minutes = d["minutes"] | int(cfg_.minutes);
        if (minutes < 1 || minutes > 120) { server_.send(400,"text/plain","Duration must be 1..120 min"); return; }
        bool ok = enqueue(action, uint8_t(minutes));
        server_.send(ok ? 202 : 409,"application/json", ok ? "{\"queued\":true}" : "{\"queued\":false}");
    });
    server_.on("/api/log", HTTP_GET, [this] { if (auth()) { server_.sendHeader("Cache-Control","no-store"); server_.send(200,"text/plain; charset=utf-8",logText()); } });
    server_.on("/api/scan", HTTP_GET, [this] {
        if (!auth()) return; JsonDocument d; int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); d["running"] = true; }
        else if (n == WIFI_SCAN_RUNNING) d["running"] = true;
        else { auto a = d["networks"].to<JsonArray>(); for (int i = 0; i < n && i < 24; ++i) { auto e = a.add<JsonObject>(); e["ssid"] = WiFi.SSID(i); e["rssi"] = WiFi.RSSI(i); } WiFi.scanDelete(); }
        sendJson(d);
    });
    server_.on("/api/restart", HTTP_POST, [this] {
        if (!auth(true)) return;
        auto s = heaterSnapshot();
        if (s.requested || s.cooldown || s.sensors.flame > 0 || otaActive.load()) { server_.send(409,"text/plain","Heater running or cooldown pending"); return; }
        // Reserve service window: no START can race a delayed reboot.
        serviceReady = restartReady = false; otaActive = true;
        uint32_t started = millis();
        while (!restartReady.load() && !wtt::elapsed(millis(),started,100)) vTaskDelay(1);
        if (!restartReady.load()) { otaActive = false; server_.send(409,"text/plain","Controller did not acknowledge safe restart"); return; }
        restartAt_ = millis(); server_.send(202,"text/plain","Restarting");
    });
    server_.on("/api/update", HTTP_POST, [this] {
        if (!auth(true)) return;
        bool ok = uploadStarted_ && uploadOk_ && !Update.hasError();
        server_.send(ok ? 200 : 400,"text/plain", ok ? "Firmware verified; restarting" : "Update rejected or failed; current firmware retained");
        if (ok) restartAt_ = millis();
        else if (uploadStarted_) { Update.abort(); otaActive = false; }
    }, [this] { upload(); });
    server_.onNotFound([this] {
        String path = server_.uri(); if (path == "/") path = "/index.html";
        for (const auto& a : webAssets) if (path == a.path) {
            if (!auth()) return;
            server_.sendHeader("Content-Encoding","gzip"); server_.sendHeader("Cache-Control","no-cache");
            server_.sendHeader("X-Content-Type-Options","nosniff");
            server_.sendHeader("Content-Security-Policy","default-src 'self'; connect-src 'self'; object-src 'none'; frame-ancestors 'none'");
            server_.send_P(200,a.mime,reinterpret_cast<const char*>(a.data),a.size); return;
        }
        if (path.startsWith("/api/")) { server_.send(404,"text/plain","Not found"); return; }
        server_.sendHeader("Location","http://192.168.4.1/"); server_.send(302,"text/plain","");
    });
}
void Network::upload() {
    auto& u = server_.upload();
    if (u.status == UPLOAD_FILE_START) {
        uploadStarted_ = uploadOk_ = false; uploadBytes_ = 0;
        if (!server_.authenticate("admin", cfg_.adminPassword.c_str()) || server_.header("X-WTT2MQTT") != "1") return;
        if (otaActive.load()) return;
        serviceReady = restartReady = false; otaActive = true;
        // Explicit acknowledgement from the bus owner prevents START/OTA snapshot races.
        uint32_t started = millis();
        while (!serviceReady.load() && !wtt::elapsed(millis(),started,100)) vTaskDelay(1);
        if (!serviceReady.load()) { otaActive = false; return; }
        // The bus task continues polling; it rejects all new starts during the upload.
        uploadStarted_ = true; uploadOk_ = Update.begin(0x1f0000, U_FLASH);
        logLine("Local OTA upload started");
    } else if (u.status == UPLOAD_FILE_WRITE && uploadStarted_ && uploadOk_) {
        uploadBytes_ += u.currentSize;
        if (uploadBytes_ > 0x1f0000 || Update.write(u.buf,u.currentSize) != u.currentSize) { uploadOk_ = false; Update.abort(); }
    } else if (u.status == UPLOAD_FILE_END && uploadStarted_) {
        uploadOk_ = uploadOk_ && uploadBytes_ > 0 && Update.end(true);
        logLine("OTA %s (%u bytes)", uploadOk_ ? "verified" : "failed", unsigned(uploadBytes_));
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        if (uploadStarted_) { Update.abort(); otaActive = false; }
        uploadStarted_ = uploadOk_ = false; logLine("OTA aborted");
    }
}
void Network::tick() {
    server_.handleClient(); uint32_t now = millis();
    if (ap_) dns_.processNextRequest();
    if (WiFi.status() == WL_CONNECTED) {
        if (ap_) { dns_.stop(); WiFi.softAPdisconnect(false); ap_ = false; logLine("WiFi connected; setup AP closed"); }
    } else if (wtt::elapsed(now,reconnectAt_,30000)) {
        startAP(); reconnectAt_ = now;
        if (!cfg_.ssid.isEmpty()) WiFi.begin(cfg_.ssid.c_str(),cfg_.wifiPassword.c_str());
    }
    if (connected_ && discoveryPending_.exchange(false)) { discovery(); mqttState(); publishedAt_ = now; }
    if (connected_ && wtt::elapsed(now,publishedAt_,uint32_t(cfg_.publishSeconds) * 1000)) { publishedAt_ = now; mqttState(); }
    if (restartAt_ && wtt::elapsed(now,restartAt_,1000)) {
        publish("availability","offline"); ESP.restart();
    }
}
