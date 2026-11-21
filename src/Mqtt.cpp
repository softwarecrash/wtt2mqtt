// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network.h"
#include "Runtime.h"
#include <WiFi.h>
void Network::mqttEvent(void* context, esp_event_base_t, int32_t eventId, void* data) {
    auto* self = static_cast<Network*>(context); auto e = static_cast<esp_mqtt_event_handle_t>(data);
    if (eventId == MQTT_EVENT_CONNECTED) {
        self->connected_ = true; self->discoveryPending_ = true;
        esp_mqtt_client_subscribe(self->mqtt_, (self->cfg_.base + "/command").c_str(), 1);
        esp_mqtt_client_subscribe(self->mqtt_, "homeassistant/status", 1);
        self->publish("availability", "online"); logLine("MQTT connected");
    } else if (eventId == MQTT_EVENT_DISCONNECTED) { self->connected_ = false; logLine("MQTT disconnected"); }
    else if (eventId == MQTT_EVENT_DATA) {
        // Reject retained/replayed commands, fragmented data and anything except an exact allowlist.
        if (e->current_data_offset || e->data_len != e->total_data_len || e->data_len > 16 || !e->topic) return;
        String topic; topic.concat(e->topic, e->topic_len);
        String payload; payload.concat(e->data, e->data_len);
        if (topic == "homeassistant/status" && payload == "online") { self->discoveryPending_ = true; return; }
        if (topic != self->cfg_.base + "/command") return;
        if (e->retain) { self->publish("command_result", "rejected_retained_command"); return; }
        if (payload == "START") {
            bool ok = enqueue(wtt::Action::Start, self->cfg_.minutes);
            self->publish("command_result", ok ? "queued_start" : "rejected_queue_or_ota");
        } else if (payload == "STOP") {
            bool ok = enqueue(wtt::Action::Stop);
            self->publish("command_result", ok ? "queued_stop" : "rejected_queue_full");
        } else self->publish("command_result", "rejected_unknown_command");
    }
}
void Network::mqttBegin() {
    if (cfg_.broker.isEmpty()) return;
    esp_mqtt_client_config_t c = {};
    c.host = cfg_.broker.c_str(); c.port = cfg_.port; c.client_id = id_.c_str();
    c.username = cfg_.mqttUser.isEmpty() ? nullptr : cfg_.mqttUser.c_str();
    c.password = cfg_.mqttPassword.isEmpty() ? nullptr : cfg_.mqttPassword.c_str();
    static String will; will = cfg_.base + "/availability";
    c.lwt_topic = will.c_str(); c.lwt_msg = "offline"; c.lwt_qos = 1; c.lwt_retain = 1;
    c.keepalive = 20; c.reconnect_timeout_ms = 5000; c.network_timeout_ms = 3000;
    c.buffer_size = 2048; c.out_buffer_size = 2048;
    mqtt_ = esp_mqtt_client_init(&c);
    if (mqtt_) { esp_mqtt_client_register_event(mqtt_, MQTT_EVENT_ANY, mqttEvent, this); esp_mqtt_client_start(mqtt_); }
    else logLine("MQTT initialization failed");
}
void Network::publish(const String& suffix, const String& value, bool retain) {
    if (!mqtt_ || !connected_) return;
    // Enqueue rather than wait for network I/O. Bound outbox growth during slow links.
    if (esp_mqtt_client_get_outbox_size(mqtt_) > 16384) return;
    String topic = cfg_.base + "/" + suffix;
    esp_mqtt_client_enqueue(mqtt_, topic.c_str(), value.c_str(), value.length(), 1, retain, true);
}
void Network::mqttState() {
    JsonDocument d; stateJson(d);
    d["wifi_rssi"] = WiFi.RSSI();
    if (cfg_.json) { String out; serializeJson(d, out); publish("status", out); }
    const char* fields[] = {"temperature","voltage","flame","power","glow_milliohm","requested","phase","communication_error","fault_count","remaining_seconds","online","command_result"};
    for (const char* key : fields) {
        String out;
        if (d[key].isNull()) out = "unknown";
        else if (d[key].is<const char*>()) out = d[key].as<String>();
        else serializeJson(d[key], out);
        publish(String("state/") + key, out);
    }
    String faults; serializeJson(d["faults"], faults); publish("state/faults", faults);
    const char* psaFields[]={"psa_online","psa_temperature_c","psa_supply_voltage_v","psa_measurement_age_seconds","psa_fault_count","psa_fault_age_seconds","psa_successes","psa_failures"};
    for(const char* key:psaFields) {
        String out;
        if(d[key].isNull()) out="unknown";
        else if(d[key].is<const char*>()) out=d[key].as<String>();
        else serializeJson(d[key],out);
        publish(String("state/")+key,out);
    }
    String psaSignals; serializeJson(d["psa_raw_signals"],psaSignals); publish("state/psa_raw_signals",psaSignals);
    String psaFaults; serializeJson(d["psa_faults"],psaFaults); publish("state/psa_faults",psaFaults);
    publish("psa_availability",d["psa_online"].as<bool>()?"online":"offline");
    publish("heater_availability", d["online"].as<bool>() && !d["temperature"].isNull() ? "online" : "offline");
}
void Network::discovery() {
    struct Entity { const char* domain; const char* key; const char* name; const char* unit; const char* deviceClass; };
    const Entity entities[] = {
        {"sensor","temperature","Kuehlmitteltemperatur","°C","temperature"},
        {"sensor","voltage","Versorgungsspannung","V","voltage"},
        {"sensor","power","Heizleistung","W","power"},
        {"sensor","glow_milliohm","Gluehstiftwiderstand","mΩ",nullptr},
        {"sensor","phase","Betriebszustand (beobachtet/abgeleitet)",nullptr,nullptr},
        {"sensor","communication_error","Kommunikationsdiagnose",nullptr,nullptr},
        {"sensor","fault_count","Gespeicherte Fehler",nullptr,nullptr},
        {"binary_sensor","flame","Flamme",nullptr,nullptr},
        {"switch","requested","Heizanforderung",nullptr,nullptr}
    };
    for (const auto& e : entities) {
        JsonDocument d;
        String topic = String("homeassistant/") + e.domain + "/" + id_ + "/" + e.key + "/config";
        if (!cfg_.discovery) { esp_mqtt_client_enqueue(mqtt_, topic.c_str(), "", 0, 1, true, true); continue; }
        d["name"] = e.name; d["unique_id"] = id_ + "_" + e.key;
        d["state_topic"] = cfg_.base + "/state/" + e.key;
        auto av = d["availability"].to<JsonArray>();
        av.add<JsonObject>()["topic"] = cfg_.base + "/availability";
        av.add<JsonObject>()["topic"] = cfg_.base + "/heater_availability";
        d["availability_mode"] = "all";
        if (e.unit) { d["unit_of_measurement"] = e.unit; d["state_class"] = "measurement"; }
        if (e.deviceClass) d["device_class"] = e.deviceClass;
        if (String(e.domain) == "binary_sensor") { d["payload_on"] = "true"; d["payload_off"] = "false"; }
        if (String(e.domain) == "switch") {
            d["command_topic"] = cfg_.base + "/command"; d["payload_on"] = "START"; d["payload_off"] = "STOP";
            d["state_on"] = "true"; d["state_off"] = "false"; d["retain"] = false;
        }
        auto dev = d["device"].to<JsonObject>(); dev["identifiers"].to<JsonArray>().add(id_);
        dev["name"] = cfg_.hostname; dev["manufacturer"] = "WTT2MQTT community";
        dev["model"] = "ESP32-S3 W-Bus bridge"; dev["sw_version"] = "0.1.0";
        String out; serializeJson(d, out);
        if (esp_mqtt_client_get_outbox_size(mqtt_) < 16384) esp_mqtt_client_enqueue(mqtt_, topic.c_str(), out.c_str(), out.length(), 1, true, true);
    }
}
