// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Config.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <mqtt_client.h>
#include <atomic>
class Network {
public:
    explicit Network(const Config& cfg): cfg_(cfg), pendingConfig_(cfg) {}
    void begin();
    void tick();
private:
    const Config& cfg_;
    Config pendingConfig_;
    WebServer server_{80}; DNSServer dns_;
    esp_mqtt_client_handle_t mqtt_ = nullptr;
    std::atomic<bool> connected_{false}, discoveryPending_{false};
    String id_, apName_, lastCommand_ = "none";
    uint32_t reconnectAt_ = 0, publishedAt_ = 0, restartAt_ = 0;
    bool ap_ = false, uploadOk_ = false, uploadStarted_ = false;
    size_t uploadBytes_ = 0;
    void startAP();
    void routes();
    bool auth(bool mutation = false);
    void sendJson(JsonDocument& doc, int status = 200);
    void publish(const String& suffix, const String& value, bool retain = true);
    void mqttBegin();
    void mqttState();
    void discovery();
    void upload();
    static void mqttEvent(void*, esp_event_base_t, int32_t, void*);
};
