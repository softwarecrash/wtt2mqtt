// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Heater.h>
#include <atomic>
struct Command { wtt::Action action; uint8_t minutes; };
extern QueueHandle_t commandQueue;
extern std::atomic<bool> otaActive;
extern std::atomic<bool> canUpdate;
extern std::atomic<bool> serviceReady;
extern std::atomic<bool> restartReady;
void logLine(const char* format, ...);
String logText();
bool scanRecording(bool active);
bool scanRecordingActive();
String scanText();
void pumpUsbLog();
void usbLine(const char* line);
wtt::Snapshot heaterSnapshot();
void storeSnapshot(const wtt::Snapshot& state);
void stateJson(JsonDocument& d);
bool enqueue(wtt::Action action, uint8_t minutes = 30);
