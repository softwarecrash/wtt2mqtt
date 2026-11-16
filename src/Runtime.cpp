// SPDX-License-Identifier: GPL-3.0-or-later
#include "Config.h"
#include "Runtime.h"
#include <stdarg.h>
#include "averias.h"
QueueHandle_t commandQueue;
std::atomic<bool> otaActive{false}, canUpdate{false};
std::atomic<bool> serviceReady{false};
std::atomic<bool> restartReady{false};
static portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
static wtt::Snapshot sharedState;
static SemaphoreHandle_t logMutex;
static QueueHandle_t usbLogs;
static char logs[48][224] = {};
static uint32_t logCount = 0;
void usbLine(const char* line) {
    char bounded[224] = {}; strlcpy(bounded,line,sizeof bounded);
    if (usbLogs) xQueueSend(usbLogs,bounded,0);
}
void storeSnapshot(const wtt::Snapshot& s) { portENTER_CRITICAL(&stateMux); sharedState = s; portEXIT_CRITICAL(&stateMux); }
wtt::Snapshot heaterSnapshot() { portENTER_CRITICAL(&stateMux); auto s = sharedState; portEXIT_CRITICAL(&stateMux); return s; }
void logLine(const char* fmt, ...) {
    if (!logMutex) { logMutex = xSemaphoreCreateMutex(); usbLogs = xQueueCreate(8,224); } // before network task
    char line[224]; int off = snprintf(line, sizeof line, "[%lu] ", (unsigned long)millis());
    va_list args; va_start(args, fmt); vsnprintf(line + off, sizeof(line) - off, fmt, args); va_end(args);
    if (xSemaphoreTake(logMutex, 0) == pdTRUE) { strlcpy(logs[logCount++ % 48], line, sizeof logs[0]); xSemaphoreGive(logMutex); }
    // Only the bus owner writes USB, avoiding availableForWrite()/write() races.
    if (usbLogs) xQueueSend(usbLogs,line,0);
}
void pumpUsbLog() {
    static char pending[225]; static size_t pos=0, count=0;
    if (!Serial) { count=pos=0; if (usbLogs) xQueueReset(usbLogs); return; }
    if (!count && usbLogs && xQueueReceive(usbLogs,pending,0)==pdTRUE) {
        count=strlen(pending); pending[count++]='\n'; pos=0;
    }
    int available=Serial.availableForWrite();
    if (count && available>0) {
        size_t n=count-pos; if (n>size_t(available)) n=size_t(available);
        pos+=Serial.write(reinterpret_cast<const uint8_t*>(pending+pos),n);
        if (pos==count) count=pos=0;
    }
}
String logText() {
    String out; out.reserve(48 * 224);
    if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        uint32_t count = logCount < 48 ? logCount : 48;
        for (uint32_t i = count; i > 0; --i) { out += logs[(logCount - i) % 48]; out += '\n'; }
        xSemaphoreGive(logMutex);
    } return out;
}
bool enqueue(wtt::Action a, uint8_t m) {
    if (otaActive.load() && a != wtt::Action::Stop) return false;
    Command c{a,m};
    return (a == wtt::Action::Stop ? xQueueSendToFront(commandQueue, &c, 0) : xQueueSend(commandQueue, &c, 0)) == pdTRUE;
}
void stateJson(JsonDocument& d) {
    auto s = heaterSnapshot();
    d["firmware"] = "0.1.0"; d["uptime"] = millis() / 1000; d["online"] = s.online;
    d["phase"] = s.phase; d["requested"] = s.requested; d["cooldown"] = s.cooldown;
    d["sensor_age_seconds"] = s.sensorsValid ? int(uint32_t(millis() - s.sensorAt) / 1000) : -1;
    d["temperature"] = nullptr; d["voltage"] = nullptr; d["flame"] = nullptr; d["power"] = nullptr; d["glow_milliohm"] = nullptr;
    if (s.sensorsValid) {
        d["temperature"] = s.sensors.temperature; d["voltage"] = s.sensors.voltage;
        if (s.sensors.flame >= 0) d["flame"] = s.sensors.flame > 0;
        if (s.sensors.power >= 0) d["power"] = s.sensors.power;
        if (s.sensors.glow >= 0) d["glow_milliohm"] = s.sensors.glow;
    }
    d["remaining_seconds"] = s.remainingSeconds; d["communication_error"] = wtt::errorText(s.error);
    d["failures"] = s.failures; d["successes"] = s.successes; d["command_result"] = s.commandResult;
    d["device"] = s.device; d["wbus_version"] = s.version; d["manufacture"] = s.manufacture;
    d["counters"] = nullptr;
    if (s.countersValid) {
        auto c = d["counters"].to<JsonObject>(); c["work_hours"] = s.workHours; c["work_minutes"] = s.workMinutes;
        c["operating_hours"] = s.operatingHours; c["operating_minutes"] = s.operatingMinutes; c["starts"] = s.starts;
    }
    d["fault_count"] = nullptr; if (s.faultsValid) d["fault_count"] = s.faultCount;
    d["fault_age_seconds"] = s.faultsValid ? int(uint32_t(millis() - s.faultAt) / 1000) : -1;
    auto faults = d["faults"].to<JsonArray>();
    if (s.faultsValid) for (int i = 0; i < s.faultCount; ++i) {
        auto f = faults.add<JsonObject>(); char code[5]; snprintf(code, sizeof code, "0x%02X", s.codes[i]);
        f["code"] = code; f["count"] = s.counts[i]; const char* text = averiaTexto(s.codes[i], 2);
        f["text"] = text ? text : "Unbekannter Fehlercode";
    }
    d["update_safe"] = canUpdate.load(); d["ota_active"] = otaActive.load(); d["free_heap"] = ESP.getFreeHeap();
}
