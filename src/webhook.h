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
#include <ArduinoJson.h>

#include "config.h"

#define WEBHOOK_QUEUE_SLOTS 8
#define WEBHOOK_TIMEOUT_MS  4000

struct WebhookEvent
{
    char from[32];
    char text[160];
    int16_t rssi;
    bool isDirect;
    uint32_t ts;
};

class WebhookSender
{
public:
    WebhookSender(GatewayConfig &cfg)
        : config(cfg), head(0), tail(0), delivered(0), failed(0), dropped(0) {}

    bool configured() const
    {
        return config.webhook.enabled && config.webhook.url[0] != '\0';
    }

    void enqueue(const char *from, const char *text, int rssi, bool isDirect)
    {
        if (!configured()) return;
        if (isDirect && !config.webhook.includeDirect) return;
        if (!isDirect && !config.webhook.includePublic) return;

        int next = (head + 1) % WEBHOOK_QUEUE_SLOTS;
        if (next == tail)
        {
            // Full: drop the oldest so the newest always gets a chance.
            tail = (tail + 1) % WEBHOOK_QUEUE_SLOTS;
            dropped++;
        }
        WebhookEvent &e = queue[head];
        strncpy(e.from, from, sizeof(e.from) - 1); e.from[sizeof(e.from) - 1] = '\0';
        strncpy(e.text, text, sizeof(e.text) - 1); e.text[sizeof(e.text) - 1] = '\0';
        e.rssi = (int16_t)rssi;
        e.isDirect = isDirect;
        e.ts = (uint32_t)time(nullptr);
        if (e.ts < 1600000000UL) e.ts = millis() / 1000;
        head = next;
    }

    // Call from loop(). Sends at most one queued event per invocation.
    void loop()
    {
        if (head == tail) return;
        if (!configured()) { tail = head; return; }   // discard if disabled mid-flight
        if (WiFi.status() != WL_CONNECTED) return;    // keep queued, retry later

        WebhookEvent e = queue[tail];
        if (post(e)) delivered++; else failed++;
        tail = (tail + 1) % WEBHOOK_QUEUE_SLOTS;
    }

    bool sendTest()
    {
        if (!configured()) return false;
        WebhookEvent e = {};
        strncpy(e.from, "(test)", sizeof(e.from) - 1);
        strncpy(e.text, "webhook test from RAK3112 gateway", sizeof(e.text) - 1);
        e.rssi = 0;
        e.isDirect = false;
        e.ts = (uint32_t)time(nullptr);
        return post(e);
    }

    uint32_t deliveredCount() const { return delivered; }
    uint32_t failedCount() const { return failed; }
    uint32_t droppedCount() const { return dropped; }
    int pending() const { return (head - tail + WEBHOOK_QUEUE_SLOTS) % WEBHOOK_QUEUE_SLOTS; }

private:
    GatewayConfig &config;
    WebhookEvent queue[WEBHOOK_QUEUE_SLOTS];
    int head, tail;
    uint32_t delivered, failed, dropped;

    bool post(const WebhookEvent &e)
    {
        StaticJsonDocument<512> d;
        d["node"] = config.repeater.nodeName;
        d["from"] = e.from;
        d["text"] = e.text;
        d["rssi"] = e.rssi;
        d["direct"] = e.isDirect;
        d["ts"] = e.ts;
        String body;
        serializeJson(d, body);

        HTTPClient http;
        http.setTimeout(WEBHOOK_TIMEOUT_MS);
        http.setConnectTimeout(WEBHOOK_TIMEOUT_MS);
        if (!http.begin(config.webhook.url)) return false;
        http.addHeader("Content-Type", "application/json");
        if (config.webhook.token[0])
        {
            http.addHeader("Authorization", String("Bearer ") + config.webhook.token);
        }
        int code = http.POST(body);
        http.end();
        return code >= 200 && code < 300;
    }
};

#endif // WEBHOOK_H
