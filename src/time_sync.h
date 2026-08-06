#ifndef TIME_SYNC_H
#define TIME_SYNC_H

// ---------------------------------------------------------------------------
// NTP time sync, independent of MQTT.
//
// The original code only synced the clock as a side effect of MQTT TLS
// certificate validation - both call sites sat behind `if (config.mqtt.useTLS)`.
// On a plaintext broker the clock was therefore never set, config.clock.autoSync
// did nothing, and everything that wants a real timestamp silently fell back:
// webhook payloads carried uptime instead of an epoch, and MeshCore adverts used
// a synthetic counter.
//
// Time matters to this firmware beyond TLS - MeshCore replay-protects adverts by
// timestamp - so it is worth syncing on its own account.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"

// Blocks up to timeoutMs. Returns true if the clock ended up set.
inline bool timeSyncNow(GatewayConfig &config, unsigned long timeoutMs = 8000)
{
    if (WiFi.status() != WL_CONNECTED) return false;

    const char *ntp = config.clock.ntpServer[0] ? config.clock.ntpServer : "pool.ntp.org";
    long gmtOffset = (long)config.clock.timezoneMinutes * 60;
    configTime(gmtOffset, 0, ntp);

    unsigned long start = millis();
    struct tm tm_info = {};
    while (millis() - start < timeoutMs)
    {
        if (getLocalTime(&tm_info, 100) && (uint32_t)time(nullptr) >= 1600000000UL) return true;
        delay(100);
    }
    return (uint32_t)time(nullptr) >= 1600000000UL;
}

inline bool timeIsValid()
{
    return (uint32_t)time(nullptr) >= 1600000000UL;
}

// ISO-8601-ish, or "-" when the clock has never been set.
inline void timeNowString(char *out, size_t n)
{
    if (!timeIsValid()) { snprintf(out, n, "-"); return; }
    time_t t = time(nullptr);
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    strftime(out, n, "%Y-%m-%d %H:%M:%S", &tm_info);
}

#endif // TIME_SYNC_H
