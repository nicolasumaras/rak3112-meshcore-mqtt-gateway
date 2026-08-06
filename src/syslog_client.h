#ifndef SYSLOG_CLIENT_H
#define SYSLOG_CLIENT_H

// ---------------------------------------------------------------------------
// Minimal RFC 3164 syslog over UDP.
//
//   <PRI>TAG: MESSAGE
//
// PRI = facility*8 + severity. Facility 16 (local0) is used throughout.
// The timestamp/hostname fields of RFC 3164 are omitted: the device often has
// no synced clock, and every collector worth using stamps on receipt anyway.
//
// Fire-and-forget by design. UDP means a missing or unreachable collector
// costs one failed send and never blocks the radio or mesh loop - which is the
// whole point of using it for a multi-day soak rather than something with a
// connection to babysit.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "config.h"

#define LOG_DEBUG 0
#define LOG_INFO  1
#define LOG_WARN  2
#define LOG_ERROR 3

class SyslogClient
{
public:
    SyslogClient(GatewayConfig &cfg) : config(cfg), ready(false), dropped(0), sent(0) {}

    void begin()
    {
        ready = false;
        if (!config.log.enabled || config.log.server[0] == '\0') return;
        if (WiFi.status() != WL_CONNECTED) return;
        udp.begin(0);   // ephemeral local port
        ready = true;
        logf(LOG_INFO, "syslog started -> %s:%u", config.log.server, config.log.port);
    }

    bool isReady() const { return ready; }
    uint32_t sentCount() const { return sent; }
    uint32_t droppedCount() const { return dropped; }

    void logf(uint8_t level, const char *fmt, ...)
    {
        if (!enabledFor(level)) return;

        char msg[220];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        send(level, msg);
    }

    void log(uint8_t level, const char *msg) { if (enabledFor(level)) send(level, msg); }

    // Periodic numeric line for the soak: the values whose *trend* matters.
    // Largest free block is included because fragmentation shows up there long
    // before total free heap moves.
    void heartbeat(uint32_t rx, uint32_t tx, uint32_t fwd, uint32_t failed, int contacts)
    {
        if (!ready) return;
        logf(LOG_INFO,
             "hb uptime=%lu heap=%u minheap=%u maxblock=%u rssi=%d rx=%lu tx=%lu fwd=%lu fail=%lu contacts=%d",
             (unsigned long)(millis() / 1000),
             (unsigned)ESP.getFreeHeap(),
             (unsigned)ESP.getMinFreeHeap(),
             (unsigned)ESP.getMaxAllocHeap(),
             (int)WiFi.RSSI(),
             (unsigned long)rx, (unsigned long)tx,
             (unsigned long)fwd, (unsigned long)failed,
             contacts);
    }

private:
    GatewayConfig &config;
    WiFiUDP udp;
    bool ready;
    uint32_t dropped;
    uint32_t sent;

    bool enabledFor(uint8_t level) const
    {
        return ready && level >= config.log.minLevel;
    }

    void send(uint8_t level, const char *msg)
    {
        if (WiFi.status() != WL_CONNECTED) { dropped++; return; }

        uint8_t severity;
        switch (level)
        {
        case LOG_ERROR: severity = 3; break;   // err
        case LOG_WARN:  severity = 4; break;   // warning
        case LOG_DEBUG: severity = 7; break;   // debug
        default:        severity = 6; break;   // info
        }
        int pri = 16 * 8 + severity;   // local0

        char line[256];
        snprintf(line, sizeof(line), "<%d>%s: %s", pri, config.repeater.nodeName, msg);

        if (!udp.beginPacket(config.log.server, config.log.port)) { dropped++; return; }
        udp.write((const uint8_t *)line, strlen(line));
        if (udp.endPacket()) sent++; else dropped++;
    }
};

#endif // SYSLOG_CLIENT_H
