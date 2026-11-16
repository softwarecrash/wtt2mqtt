// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network.h"
#include "Runtime.h"
#include "WebAssets.h"
#include "SetupAccess.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
void Network::startAP() {
    if (ap_) return;
    WiFi.mode(WIFI_AP_STA); WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    ap_ = WiFi.softAP(apName_.c_str());
    if (ap_) dns_.start(53, "*", IPAddress(192,168,4,1));
    logLine("Open setup AP %s at 192.168.4.1", apName_.c_str());
}
void Network::begin() {
    char ident[32]; snprintf(ident, sizeof ident, "wtt2mqtt-%012llx", ESP.getEfuseMac());
    id_ = ident; apName_ = "wtt2mqtt";
    WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setHostname(wifiHostname_.c_str()); WiFi.setSleep(false); WiFi.setAutoReconnect(true);
    if (!wifiSsid_.isEmpty()) WiFi.begin(wifiSsid_.c_str(), wifiPassword_.c_str()); else startAP();
    offlineAt_ = reconnectAt_ = millis();
    if (MDNS.begin(wifiHostname_.c_str())) MDNS.addService("http", "tcp", 80);
    routes(); server_.begin(); mqttBegin();
}
bool Network::isSetupRequest() {
    return ap_ && server_.client().localIP() == WiFi.softAPIP();
}
bool Network::auth(bool mutation) {
    if (isSetupRequest() && !setupAccess::allowed(server_.uri().c_str(),server_.method() == HTTP_GET,server_.method() == HTTP_POST)) {
        server_.send(403,"text/plain","Setup AP: WiFi configuration only"); return false;
    }
    if (cfg_.webAuthEnabled() && !isSetupRequest() && !server_.authenticate(cfg_.adminUser.c_str(), cfg_.adminPassword.c_str())) { server_.requestAuthentication(); return false; }
    if (mutation && server_.header("X-WTT2MQTT") != "1") { server_.send(403, "text/plain", "X-WTT2MQTT header required"); return false; }
    return true;
}
void Network::sendJson(JsonDocument& doc, int status) {
    String s; serializeJson(doc, s); server_.sendHeader("Cache-Control", "no-store"); server_.send(status, "application/json", s);
}
void Network::routes() {
    const char* headers[] = {"X-WTT2MQTT"}; server_.collectHeaders(headers, 1);
    server_.on("/api/wifi", HTTP_GET, [this] {
        if (!auth()) return; JsonDocument d;
        d["ssid"] = pendingConfig_.ssid; d["hostname"] = pendingConfig_.hostname;
        d["wifiPasswordSet"] = !pendingConfig_.wifiPassword.isEmpty();
        d["connected"] = WiFi.status() == WL_CONNECTED;
        d["retry_seconds"] = 30; sendJson(d);
    });
    server_.on("/api/wifi", HTTP_POST, [this] {
        if (!auth(true)) return;
        if (otaActive.load()) { server_.send(409,"text/plain","Service operation in progress"); return; }
        if (server_.arg("plain").length() > 512) { server_.send(413,"text/plain","Request too large"); return; }
        JsonDocument d; auto parseError = deserializeJson(d,server_.arg("plain"));
        if (parseError || !d.is<JsonObject>()) { server_.send(400,"text/plain","WiFi JSON object required"); return; }
        for (JsonPairConst entry : d.as<JsonObjectConst>()) if (!setupAccess::wifiField(entry.key().c_str())) {
            server_.send(403,"text/plain","Only ssid, wifiPassword and hostname may be changed here"); return;
        }
        Config next = pendingConfig_; String error;
        if (!next.update(d.as<JsonVariantConst>(),error)) { server_.send(400,"text/plain",error); return; }
        if (!next.save()) { server_.send(500,"text/plain","NVS write failed"); return; }
        pendingConfig_ = next; wifiApplyPending_ = true; wifiApplyAt_ = millis();
        server_.send(200,"application/json","{\"saved\":true,\"reconnecting\":true}");
        logLine("WiFi settings saved; applying without reboot");
    });
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
        if (s.requested || otaActive.load()) { server_.send(409,"text/plain","Stop the active heating request before restarting"); return; }
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
        String path = server_.uri();
        if (isSetupRequest()) {
            if (path == "/" || path == "/index.html") path = "/setup.html";
            if (!setupAccess::asset(path.c_str())) {
                if (path.startsWith("/api/") || path == "/app.js") { server_.send(403,"text/plain","Setup AP: WiFi configuration only"); return; }
                server_.sendHeader("Location","http://192.168.4.1/"); server_.send(302,"text/plain",""); return;
            }
        } else if (path == "/") path = "/index.html";
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
        if (isSetupRequest() || (cfg_.webAuthEnabled() && !server_.authenticate(cfg_.adminUser.c_str(), cfg_.adminPassword.c_str())) || server_.header("X-WTT2MQTT") != "1") return;
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
    if (wifiApplyPending_ && wtt::elapsed(now,wifiApplyAt_,1000)) {
        wifiApplyPending_ = false;
        wifiSsid_ = pendingConfig_.ssid; wifiPassword_ = pendingConfig_.wifiPassword; wifiHostname_ = pendingConfig_.hostname;
        // Keep the AP available while trying the new credentials. Do not reboot
        // or interrupt the independently running heater control.
        startAP(); WiFi.disconnect(false,false); WiFi.setHostname(wifiHostname_.c_str());
        MDNS.end(); if (MDNS.begin(wifiHostname_.c_str())) MDNS.addService("http","tcp",80);
        offlineAt_ = reconnectAt_ = now; wifiWasConnected_ = false;
        if (!wifiSsid_.isEmpty()) WiFi.begin(wifiSsid_.c_str(),wifiPassword_.c_str());
    }
    if (WiFi.status() == WL_CONNECTED) {
        wifiWasConnected_ = true; offlineAt_ = reconnectAt_ = now;
        // Switch off the AP interface, rather than merely clearing its config.
        // softAPdisconnect(false) leaves AP mode enabled in Arduino 2.0.17.
        if (ap_ && WiFi.mode(WIFI_STA)) { dns_.stop(); ap_ = false; logLine("WiFi connected; setup AP closed"); }
    } else {
        if (wifiWasConnected_) { wifiWasConnected_ = false; offlineAt_ = reconnectAt_ = now; logLine("WiFi lost; reconnecting, setup AP after 30 s"); }
        if (wtt::elapsed(now,offlineAt_,30000)) startAP();
        if (!wifiSsid_.isEmpty() && wtt::elapsed(now,reconnectAt_,30000) && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
            reconnectAt_ = now; WiFi.begin(wifiSsid_.c_str(),wifiPassword_.c_str());
        }
    }
    if (connected_ && discoveryPending_.exchange(false)) { discovery(); mqttState(); publishedAt_ = now; }
    if (connected_ && wtt::elapsed(now,publishedAt_,uint32_t(cfg_.publishSeconds) * 1000)) { publishedAt_ = now; mqttState(); }
    if (restartAt_ && wtt::elapsed(now,restartAt_,1000)) {
        publish("availability","offline"); ESP.restart();
    }
}
