// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string.h>
namespace setupAccess {
// One allowlist for the open AP, checked before authentication or request handling.
inline bool asset(const char* path) {
    return path && (!strcmp(path,"/setup.html") || !strcmp(path,"/setup.js") || !strcmp(path,"/style.css"));
}
inline bool allowed(const char* path, bool get, bool post) {
    if (!path) return false;
    if (get) return asset(path) || !strcmp(path,"/") || !strcmp(path,"/index.html") ||
        !strcmp(path,"/api/wifi") || !strcmp(path,"/api/scan");
    return post && !strcmp(path,"/api/wifi");
}
inline bool wifiField(const char* key) {
    return key && (!strcmp(key,"ssid") || !strcmp(key,"wifiPassword") || !strcmp(key,"hostname"));
}
}
