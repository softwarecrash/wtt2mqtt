// SPDX-License-Identifier: GPL-3.0-or-later
// Access-policy checks only; no heater model or simulator.
#include "SetupAccess.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static int checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); std::exit(1); } } while (0)
int main() {
    for (const char* path : {"/","/index.html","/setup.html","/setup.js","/style.css","/api/wifi","/api/scan"})
        CHECK(setupAccess::allowed(path,true,false));
    CHECK(setupAccess::allowed("/api/wifi",false,true));
    for (const char* path : {"/api/status","/api/config","/api/command","/api/log","/api/restart","/api/update","/app.js","/api/mqtt","/unknown"}) {
        CHECK(!setupAccess::allowed(path,true,false)); CHECK(!setupAccess::allowed(path,false,true));
        CHECK(!setupAccess::asset(path));
    }
    CHECK(!setupAccess::allowed("/api/scan",false,true));
    CHECK(!setupAccess::allowed("/setup.html",false,true));
    CHECK(!setupAccess::allowed("/api/wifi",false,false));
    CHECK(!setupAccess::allowed(nullptr,true,false));
    for (const char* key : {"ssid","wifiPassword","hostname"}) CHECK(setupAccess::wifiField(key));
    for (const char* key : {"broker","port","mqttPassword","adminUser","adminPassword","rx","tx","minutes","webAuthVersion"})
        CHECK(!setupAccess::wifiField(key));
    std::printf("PASS %d AP access checks: only WLAN routes/assets and WLAN fields allowed\n",checks);
}
