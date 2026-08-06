#ifndef WEBHOOK_H
#define WEBHOOK_H

// ---------------------------------------------------------------------------
// Outbound webhook for received MeshCore messages.
//
// Deliveries are queued on receipt and drained from loop(), never sent inline
// from the packet handler: an HTTP POST to an unreachable host can block for
// seconds, and the radio would miss traffic for the whole of it.
//
// One delivery per loop pass, so a slow endpoint degrades throughput rather
// than stalling the mesh. The queue drops oldest-first when full, because the
// mesh is the system of record - a webhook is a convenience.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "config.h"

#define WEBHOOK_QUEUE_SLOTS 16
#define WEBHOOK_TIMEOUT_MS  4000

struct WebhookEvent
{
    char from[32];
    char text[160];
    int16_t rssi;
    bool isDirect;
    uint32_t ts;
    uint32_t id;        // strictly increasing per delivery
    uint32_t uptime;    // seconds since boot, always meaningful
    bool clockValid;    // false when ts is uptime, not a real epoch
    int8_t target;      // which webhook slot this delivery is for
};

class WebhookSender
{
public:
    typedef void (*LogFn)(const char *msg);

    WebhookSender(GatewayConfig &cfg)
        : config(cfg), head(0), tail(0), delivered(0), failed(0), dropped(0), logger(NULL) {}

    void setLogger(LogFn fn) { logger = fn; }

    bool configured(int i) const
    {
        return i >= 0 && i < MAX_WEBHOOKS &&
               config.webhooks[i].enabled && config.webhooks[i].url[0] != '\0';
    }

    bool anyConfigured() const
    {
        for (int i = 0; i < MAX_WEBHOOKS; ++i) if (configured(i)) return true;
        return false;
    }

    // One queue entry per matching webhook. Each endpoint gets its own delivery
    // and its own success/failure accounting, so a broken endpoint cannot mask
    // or hold up a working one.
    void enqueue(const char *from, const char *text, int rssi, bool isDirect)
    {
        const char *kind = isDirect ? "direct" : "public";
        if (!anyConfigured()) { note("hook skip %s: no webhooks configured", kind); return; }

        uint32_t id = ++seq;
        int queued = 0;
        for (int i = 0; i < MAX_WEBHOOKS; ++i)
        {
            if (!configured(i)) continue;
            if (isDirect && !config.webhooks[i].includeDirect) continue;
            if (!isDirect && !config.webhooks[i].includePublic) continue;

            int next = (head + 1) % WEBHOOK_QUEUE_SLOTS;
            if (next == tail)
            {
                tail = (tail + 1) % WEBHOOK_QUEUE_SLOTS;
                dropped++;
            }
            WebhookEvent &e = queue[head];
            strncpy(e.from, from, sizeof(e.from) - 1); e.from[sizeof(e.from) - 1] = '\0';
            strncpy(e.text, text, sizeof(e.text) - 1); e.text[sizeof(e.text) - 1] = '\0';
            e.rssi = (int16_t)rssi;
            e.isDirect = isDirect;
            e.uptime = millis() / 1000;
            e.ts = (uint32_t)time(nullptr);
            e.clockValid = (e.ts >= 1600000000UL);
            if (!e.clockValid) e.ts = e.uptime;
            e.id = id;              // same id across endpoints: it identifies the message
            e.target = (int8_t)i;
            head = next;
            queued++;
        }
        if (queued) note("hook queue %s from=%s len=%u -> %d endpoint(s)",
                         kind, from, (unsigned)strlen(text), queued);
        else        note("hook skip %s: filtered off everywhere", kind);
    }

    // Call from loop(). Sends at most one queued event per invocation.
    void loop()
    {
        if (head == tail) return;
        if (WiFi.status() != WL_CONNECTED) return;    // keep queued, retry later

        WebhookEvent e = queue[tail];
        tail = (tail + 1) % WEBHOOK_QUEUE_SLOTS;

        if (!configured(e.target)) return;            // disabled since it was queued

        int code = post(e);
        if (code >= 200 && code < 300) { delivered++; stats[e.target].delivered++; }
        else                           { failed++;    stats[e.target].failed++; }
        note("hook post #%d %s id=%lu status=%d %s", e.target + 1,
             e.isDirect ? "direct" : "public", (unsigned long)e.id, code, lastErr);
    }

    // Returns the HTTP status, or a negative HTTPClient error. lastError() holds
    // a human-readable reason - a bare bool told the operator nothing about
    // whether it was DNS, TLS, a refused connection or a 404.
    int sendTest(int i)
    {
        if (!configured(i)) { snprintf(lastErr, sizeof(lastErr), "not configured"); return -1000; }
        WebhookEvent e = {};
        strncpy(e.from, "(test)", sizeof(e.from) - 1);
        strncpy(e.text, "webhook test from RAK3112 gateway", sizeof(e.text) - 1);
        e.rssi = 0;
        e.isDirect = false;
        e.uptime = millis() / 1000;
        e.ts = (uint32_t)time(nullptr);
        e.clockValid = (e.ts >= 1600000000UL);
        if (!e.clockValid) e.ts = e.uptime;
        e.id = ++seq;
        e.target = (int8_t)i;
        return post(e);
    }

    const char *lastError() const { return lastErr; }

    uint32_t deliveredCount() const { return delivered; }
    uint32_t failedCount() const { return failed; }
    uint32_t droppedCount() const { return dropped; }
    uint32_t deliveredCount(int i) const { return (i >= 0 && i < MAX_WEBHOOKS) ? stats[i].delivered : 0; }
    uint32_t failedCount(int i) const { return (i >= 0 && i < MAX_WEBHOOKS) ? stats[i].failed : 0; }
    int pending() const { return (head - tail + WEBHOOK_QUEUE_SLOTS) % WEBHOOK_QUEUE_SLOTS; }

private:
    GatewayConfig &config;
    WebhookEvent queue[WEBHOOK_QUEUE_SLOTS];
    int head, tail;
    uint32_t delivered, failed, dropped;
    struct PerHook { uint32_t delivered = 0; uint32_t failed = 0; };
    PerHook stats[MAX_WEBHOOKS];
    uint32_t seq = 0;
    char lastErr[96] = {0};
    LogFn logger;

    void note(const char *fmt, ...)
    {
        if (!logger) return;
        char buf[160];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        logger(buf);
    }

    static const char *httpcError(int code)
    {
        switch (code)
        {
        case HTTPC_ERROR_CONNECTION_REFUSED: return "connection refused";
        case HTTPC_ERROR_SEND_HEADER_FAILED: return "send header failed";
        case HTTPC_ERROR_SEND_PAYLOAD_FAILED: return "send payload failed";
        case HTTPC_ERROR_NOT_CONNECTED: return "not connected";
        case HTTPC_ERROR_CONNECTION_LOST: return "connection lost";
        case HTTPC_ERROR_NO_STREAM: return "no stream";
        case HTTPC_ERROR_NO_HTTP_SERVER: return "no HTTP server at that address";
        case HTTPC_ERROR_TOO_LESS_RAM: return "out of memory";
        case HTTPC_ERROR_ENCODING: return "bad transfer encoding";
        case HTTPC_ERROR_STREAM_WRITE: return "stream write failed";
        case HTTPC_ERROR_READ_TIMEOUT: return "read timeout";
        default: return "unknown error";
        }
    }

    int post(const WebhookEvent &e)
    {
        StaticJsonDocument<512> d;
        d["node"] = config.repeater.nodeName;
        d["from"] = e.from;
        d["text"] = e.text;
        d["rssi"] = e.rssi;
        d["direct"] = e.isDirect;
        d["ts"] = e.ts;
        // Repeated identical texts from the same node are normal - "weather"
        // twice is two requests, not a duplicate. Receivers need something
        // unambiguous to key on, and ts alone is not it: without NTP it is an
        // uptime counter, and it resets on reboot.
        d["id"] = e.id;
        d["uptime"] = e.uptime;
        d["clockValid"] = e.clockValid;
        String body;
        serializeJson(d, body);

        const WebhookConfig &wh = config.webhooks[e.target < 0 ? 0 : e.target];
        bool isTls = strncmp(wh.url, "https://", 8) == 0;

        // begin(url) with no client cannot do TLS on ESP32 - an https endpoint
        // fails before a single byte is sent. Give it a secure client explicitly.
        WiFiClient plain;
        WiFiClientSecure secure;
        if (isTls)
        {
            // No cert store on the device. This authenticates the payload via the
            // bearer token, not the server, so a hostile endpoint could still
            // impersonate the receiver. Documented rather than silently assumed.
            secure.setInsecure();
            secure.setTimeout(WEBHOOK_TIMEOUT_MS / 1000);
        }

        HTTPClient http;
        http.setTimeout(WEBHOOK_TIMEOUT_MS);
        http.setConnectTimeout(WEBHOOK_TIMEOUT_MS);
        http.setReuse(false);

        bool ok = isTls ? http.begin(secure, wh.url)
                        : http.begin(plain, wh.url);
        if (!ok)
        {
            snprintf(lastErr, sizeof(lastErr), "could not parse URL (need http:// or https://)");
            return -1001;
        }

        http.addHeader("Content-Type", "application/json");
        if (wh.token[0])
        {
            http.addHeader("Authorization", String("Bearer ") + wh.token);
        }
        int code = http.POST(body);
        http.end();

        if (code < 0)
        {
            snprintf(lastErr, sizeof(lastErr), "%s (%d)", httpcError(code), code);
        }
        else if (code < 200 || code >= 300)
        {
            snprintf(lastErr, sizeof(lastErr), "endpoint returned HTTP %d", code);
        }
        else
        {
            snprintf(lastErr, sizeof(lastErr), "ok (HTTP %d)", code);
        }
        return code;
    }
};

#endif // WEBHOOK_H
