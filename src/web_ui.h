#ifndef WEB_UI_H
#define WEB_UI_H

// ---------------------------------------------------------------------------
// Password-protected web UI for MeshCore messaging.
//
//   GET  /            single-page UI
//   GET  /api/state   identity, contacts, messages  (JSON)
//   POST /api/send    {"text": "...", "to": <contact idx or -1 for public>}
//   POST /api/advert  broadcast a signed self-advert
//
// Auth is HTTP Basic against config.security.adminPassword. The page can inject
// traffic into the mesh, so it is refused outright when no password is set
// rather than silently running open.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>

#include "config.h"
#include "settings_manager.h"
#include "meshcore_proto.h"
#include "webhook.h"
#include "time_sync.h"

typedef bool (*WebSendFn)(const uint8_t *data, size_t len);
typedef void (*WebLogFn)(const char *msg);

class MeshWebUI
{
public:
    MeshWebUI(GatewayConfig &cfg, SettingsManager &sm, MeshCoreProto &proto,
              WebhookSender &wh, WebSendFn sendFn)
        : server(80), config(cfg), settings(sm), mesh(proto), hook(wh),
          send(sendFn), started(false) {}

    bool begin()
    {
        if (strlen(config.security.adminPassword) == 0)
        {
            Serial.println(F("⚠ Web UI disabled: no admin password set (menu 10)."));
            Serial.println(F("  The page can transmit into the mesh, so it will not run open."));
            return false;
        }

        server.on("/", HTTP_GET, [this]() { handleRoot(); });
        server.on("/api/state", HTTP_GET, [this]() { handleState(); });
        server.on("/api/send", HTTP_POST, [this]() { handleSend(); });
        server.on("/api/advert", HTTP_POST, [this]() { handleAdvert(); });
        server.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
        server.on("/api/config", HTTP_POST, [this]() { handleSetConfig(); });
        server.on("/api/restart", HTTP_POST, [this]() { handleRestart(); });
        // Public API
        server.on("/api/contacts", HTTP_GET, [this]() { handleContacts(); });
        server.on("/api/messages", HTTP_GET, [this]() { handleMessages(); });
        server.on("/api/messages", HTTP_POST, [this]() { handleSend(); });
        server.on("/api/webhooks", HTTP_GET, [this]() { handleGetWebhooks(); });
        server.on("/api/webhooks", HTTP_POST, [this]() { handleSetWebhooks(); });
        server.on("/api/webhooks/test", HTTP_POST, [this]() { handleTestWebhook(); });
        server.on("/api/timesync", HTTP_POST, [this]() { handleTimeSync(); });
        server.on("/api/update", HTTP_GET, [this]() { handleUpdateInfo(); });
        server.on("/api/update", HTTP_POST,
                  [this]() { handleUpdateDone(); },
                  [this]() { handleUpdateUpload(); });
        server.onNotFound([this]() { server.send(404, "text/plain", "not found"); });
        server.begin();
        started = true;
        return true;
    }

    void loop()
    {
        if (started) server.handleClient();
    }

    bool isStarted() const { return started; }
    void setLogger(WebLogFn fn) { apiLog = fn; }

private:
    WebServer server;
    GatewayConfig &config;
    SettingsManager &settings;
    MeshCoreProto &mesh;
    WebhookSender &hook;
    WebSendFn send;
    bool started;
    WebLogFn apiLog = nullptr;
    bool otaAuthOk = false;
    String otaError;
    size_t otaBytes = 0;

    bool authed()
    {
        if (!server.authenticate("admin", config.security.adminPassword))
        {
            server.requestAuthentication();
            return false;
        }
        return true;
    }

    uint32_t nowTs()
    {
        uint32_t ts = (uint32_t)time(nullptr);
        if (ts < 1600000000UL) ts = millis() / 1000;
        return ts;
    }

    void handleRoot()
    {
        if (!authed()) return;
        server.send_P(200, "text/html", PAGE);
    }

    void handleState()
    {
        if (!authed()) return;

        StaticJsonDocument<4096> doc;
        char hex[8];
        snprintf(hex, sizeof(hex), "0x%02X", mesh.selfHash());
        doc["self"] = hex;
        doc["name"] = config.repeater.nodeName;
        snprintf(hex, sizeof(hex), "0x%02X", mesh.channelHash());
        doc["channel"] = hex;

        JsonArray cs = doc.createNestedArray("contacts");
        for (int i = 0; i < MC_MAX_CONTACTS; ++i)
        {
            if (!mesh.contacts[i].used) continue;
            JsonObject o = cs.createNestedObject();
            o["idx"] = i;
            o["name"] = mesh.contacts[i].name;
            o["rssi"] = mesh.contacts[i].lastRssi;
        }

        JsonArray ms = doc.createNestedArray("messages");
        int n = mesh.messageCount();
        if (n > 20) n = 20;
        for (int i = 0; i < n; ++i)
        {
            const MCMessage &m = mesh.messageAt(i);
            JsonObject o = ms.createNestedObject();
            o["from"] = m.from;
            o["text"] = m.text;
            o["rssi"] = m.rssi;
            o["direct"] = m.isDirect;
            o["out"] = m.outgoing;
            o["ts"] = m.timestamp;
        }

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    }

    void handleSend()
    {
        if (!authed()) return;

        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok)
        {
            server.send(400, "application/json", "{\"error\":\"bad json\"}");
            return;
        }
        const char *text = doc["text"] | "";
        int to = doc["to"] | -1;
        if (strlen(text) == 0)
        {
            server.send(400, "application/json", "{\"error\":\"empty text\"}");
            return;
        }

        uint8_t frame[255];
        size_t n = 0;
        bool direct = false;
        const char *label = "(public)";

        if (to < 0)
        {
            n = mesh.buildGroupText(frame, sizeof(frame), text, nowTs());
        }
        else
        {
            if (to >= MC_MAX_CONTACTS || !mesh.contacts[to].used)
            {
                server.send(400, "application/json", "{\"error\":\"unknown contact\"}");
                return;
            }
            n = mesh.buildDirectText(frame, sizeof(frame), mesh.contacts[to], text, nowTs());
            direct = true;
            label = mesh.contacts[to].name;
        }

        if (n == 0)
        {
            if (apiLog) apiLog("api send REJECTED: could not build frame");
            server.send(500, "application/json", "{\"error\":\"could not build frame\"}");
            return;
        }
        if (!send(frame, n))
        {
            if (apiLog)
            {
                char b[128];
                snprintf(b, sizeof(b), "api send FAILED to=%s len=%u (radio refused)",
                         label, (unsigned)strlen(text));
                apiLog(b);
            }
            server.send(500, "application/json", "{\"error\":\"transmit failed\"}");
            return;
        }
        mesh.recordOutgoing(label, text, direct);
        if (apiLog)
        {
            char b[128];
            snprintf(b, sizeof(b), "api send ok to=%s len=%u frame=%u",
                     label, (unsigned)strlen(text), (unsigned)n);
            apiLog(b);
        }
        server.send(200, "application/json", "{\"ok\":true}");
    }

    void handleAdvert()
    {
        if (!authed()) return;

        // Adverts are replay-protected by timestamp, so the value must strictly
        // increase across reboots. Real time when NTP has synced, otherwise a
        // persisted counter offset past the replay-guard threshold.
        uint32_t ts = (uint32_t)time(nullptr);
        if (ts < 1600000000UL) ts = 1600000000UL + mesh.nextAdvertSeq();

        uint8_t frame[255];
        bool hasLoc = (config.location.latitude != 0.0 || config.location.longitude != 0.0);
        size_t n = mesh.buildAdvert(frame, sizeof(frame), ts, hasLoc,
                                    config.location.latitude, config.location.longitude);
        if (n == 0 || !send(frame, n))
        {
            server.send(500, "application/json", "{\"error\":\"advert failed\"}");
            return;
        }
        server.send(200, "application/json", "{\"ok\":true}");
    }

    void handleContacts()
    {
        if (!authed()) return;
        StaticJsonDocument<2048> d;
        JsonArray a = d.to<JsonArray>();
        for (int i = 0; i < MC_MAX_CONTACTS; ++i)
        {
            if (!mesh.contacts[i].used) continue;
            JsonObject o = a.createNestedObject();
            o["id"] = i;
            o["name"] = mesh.contacts[i].name;
            o["rssi"] = mesh.contacts[i].lastRssi;
            char h[8];
            snprintf(h, sizeof(h), "0x%02X", mesh.contacts[i].pubKey[0]);
            o["hash"] = h;
            o["lastAdvert"] = mesh.contacts[i].lastAdvert;
        }
        String out; serializeJson(d, out);
        server.send(200, "application/json", out);
    }

    void handleMessages()
    {
        if (!authed()) return;
        int limit = server.hasArg("limit") ? server.arg("limit").toInt() : 20;
        if (limit < 1) limit = 1;
        if (limit > MC_MAX_MESSAGES) limit = MC_MAX_MESSAGES;

        StaticJsonDocument<4096> d;
        JsonArray a = d.to<JsonArray>();
        int n = mesh.messageCount();
        if (n > limit) n = limit;
        for (int i = 0; i < n; ++i)
        {
            const MCMessage &m = mesh.messageAt(i);
            JsonObject o = a.createNestedObject();
            o["from"] = m.from;
            o["text"] = m.text;
            o["rssi"] = m.rssi;
            o["direct"] = m.isDirect;
            o["outgoing"] = m.outgoing;
            o["ts"] = m.timestamp;
        }
        String out; serializeJson(d, out);
        server.send(200, "application/json", out);
    }

    void fingerprintOf(const char *tok, char *out, size_t n)
    {
        if (!tok || tok[0] == '\0') { snprintf(out, n, "-"); return; }
        SHA256 sha;
        uint8_t h[32];
        sha.reset();
        sha.update((const uint8_t *)tok, strlen(tok));
        sha.finalize(h, sizeof(h));
        snprintf(out, n, "%02x%02x%02x%02x", h[0], h[1], h[2], h[3]);
    }

    void webhooksToJson(JsonArray a)
    {
        for (int i = 0; i < MAX_WEBHOOKS; ++i)
        {
            JsonObject o = a.createNestedObject();
            char fp[16]; fingerprintOf(config.webhooks[i].token, fp, sizeof(fp));
            o["id"] = i;
            o["enabled"] = config.webhooks[i].enabled;
            o["url"] = config.webhooks[i].url;
            o["hasToken"] = strlen(config.webhooks[i].token) > 0;
            o["tokenLength"] = (int)strlen(config.webhooks[i].token);
            o["tokenFingerprint"] = fp;
            o["includePublic"] = config.webhooks[i].includePublic;
            o["includeDirect"] = config.webhooks[i].includeDirect;
            o["delivered"] = hook.deliveredCount(i);
            o["failed"] = hook.failedCount(i);
        }
    }

    void handleGetWebhooks()
    {
        if (!authed()) return;
        StaticJsonDocument<3072> d;
        JsonObject root = d.to<JsonObject>();
        webhooksToJson(root.createNestedArray("webhooks"));
        root["dropped"] = hook.droppedCount();
        root["pending"] = hook.pending();
        root["max"] = MAX_WEBHOOKS;
        String out; serializeJson(d, out);
        server.send(200, "application/json", out);
    }

    // Accepts {"webhooks":[{"id":0,...},...]} - only the ids present are touched,
    // so a partial update cannot silently blank the others.
    bool applyWebhooks(JsonArray arr, String &err)
    {
        for (JsonObject w : arr)
        {
            int i = w["id"] | -1;
            if (i < 0 || i >= MAX_WEBHOOKS) { err = "bad webhook id"; return false; }
            if (w.containsKey("url")) copyStr(config.webhooks[i].url, sizeof(config.webhooks[i].url), w["url"]);
            if (!setSecret(config.webhooks[i].token, sizeof(config.webhooks[i].token), w["token"]))
            {
                err = "token too long (max 128 characters)";
                return false;
            }
            if (w.containsKey("enabled")) config.webhooks[i].enabled = w["enabled"];
            if (w.containsKey("includePublic")) config.webhooks[i].includePublic = w["includePublic"];
            if (w.containsKey("includeDirect")) config.webhooks[i].includeDirect = w["includeDirect"];
            if (w["clearToken"] | false) config.webhooks[i].token[0] = '\0';
            if (config.webhooks[i].url[0] == '\0') config.webhooks[i].enabled = false;
        }
        return true;
    }

    void handleSetWebhooks()
    {
        if (!authed()) return;
        StaticJsonDocument<3072> d;
        if (deserializeJson(d, server.arg("plain")) != DeserializationError::Ok)
        {
            server.send(400, "application/json", "{\"error\":\"bad json\"}");
            return;
        }
        JsonArray arr = d["webhooks"].as<JsonArray>();
        if (arr.isNull()) { server.send(400, "application/json", "{\"error\":\"expected webhooks array\"}"); return; }

        String err;
        if (!applyWebhooks(arr, err))
        {
            server.send(400, "application/json", String("{\"error\":\"") + err + "\"}");
            return;
        }
        if (!settings.saveConfig(config))
        {
            server.send(500, "application/json", "{\"error\":\"save failed\"}");
            return;
        }
        server.send(200, "application/json", "{\"ok\":true}");
    }

    void handleTestWebhook()
    {
        if (!authed()) return;
        int i = server.hasArg("id") ? server.arg("id").toInt() : 0;
        if (i < 0 || i >= MAX_WEBHOOKS)
        {
            server.send(400, "application/json", "{\"error\":\"bad webhook id\"}");
            return;
        }
        int code = hook.sendTest(i);
        StaticJsonDocument<256> d;
        bool ok = (code >= 200 && code < 300);
        d["ok"] = ok;
        d["id"] = i;
        d["status"] = code;
        d[ok ? "detail" : "error"] = hook.lastError();
        String out; serializeJson(d, out);
        server.send(ok ? 200 : 502, "application/json", out);
    }

    // ---- OTA -------------------------------------------------------------
    //
    // An unauthenticated firmware endpoint is remote code execution, so auth is
    // checked at UPLOAD_FILE_START, before a single byte is accepted, and again
    // in the completion handler. A failed or aborted update leaves the running
    // partition untouched: Update writes to the inactive OTA slot and only
    // switches the boot partition on a verified end().

    void handleUpdateInfo()
    {
        if (!authed()) return;
        StaticJsonDocument<384> d;
        d["running"] = esp_ota_get_running_partition()->label;
        d["sketchSize"] = ESP.getSketchSize();
        d["sketchMD5"] = ESP.getSketchMD5();
        d["freeSpace"] = ESP.getFreeSketchSpace();
        d["canUpdate"] = (esp_ota_get_next_update_partition(NULL) != NULL);
        String out; serializeJson(d, out);
        server.send(200, "application/json", out);
    }

    void handleUpdateUpload()
    {
        HTTPUpload &up = server.upload();

        if (up.status == UPLOAD_FILE_START)
        {
            otaAuthOk = server.authenticate("admin", config.security.adminPassword);
            otaError = "";
            otaBytes = 0;
            if (!otaAuthOk) { otaError = "unauthorised"; return; }

            if (esp_ota_get_next_update_partition(NULL) == NULL)
            {
                otaError = "no OTA partition available";
                return;
            }
            if (apiLog) apiLog("ota start");
            if (!Update.begin(UPDATE_SIZE_UNKNOWN))
            {
                otaError = Update.errorString();
                return;
            }
            // Optional integrity check: /api/update?md5=<32 hex>
            if (server.hasArg("md5"))
            {
                String m = server.arg("md5");
                if (m.length() == 32) Update.setMD5(m.c_str());
            }
        }
        else if (up.status == UPLOAD_FILE_WRITE)
        {
            if (!otaAuthOk || otaError.length()) return;

            // An ESP32 image starts 0xE9. Rejecting here turns "uploaded the
            // wrong file" into an error instead of a bricked board.
            if (otaBytes == 0 && up.currentSize > 0 && up.buf[0] != 0xE9)
            {
                otaError = "not an ESP32 firmware image (bad magic byte)";
                Update.abort();
                return;
            }
            if (Update.write(up.buf, up.currentSize) != up.currentSize)
            {
                otaError = Update.errorString();
                Update.abort();
                return;
            }
            otaBytes += up.currentSize;
        }
        else if (up.status == UPLOAD_FILE_END)
        {
            if (!otaAuthOk || otaError.length()) return;
            if (!Update.end(true)) otaError = Update.errorString();
        }
        else if (up.status == UPLOAD_FILE_ABORTED)
        {
            Update.abort();
            otaError = "upload aborted";
        }
    }

    void handleUpdateDone()
    {
        if (!otaAuthOk)
        {
            server.requestAuthentication();
            return;
        }
        StaticJsonDocument<256> d;
        bool ok = (otaError.length() == 0 && otaBytes > 0);
        d["ok"] = ok;
        d["bytes"] = otaBytes;
        if (!ok) d["error"] = otaError.length() ? otaError : String("no data received");
        String out; serializeJson(d, out);

        if (apiLog)
        {
            char b[160];
            snprintf(b, sizeof(b), "ota %s bytes=%u %s", ok ? "ok" : "FAILED",
                     (unsigned)otaBytes, ok ? "rebooting" : otaError.c_str());
            apiLog(b);
        }

        server.send(ok ? 200 : 400, "application/json", out);
        if (ok)
        {
            delay(600);      // let the response flush before the reboot
            ESP.restart();
        }
    }

    void handleTimeSync()
    {
        if (!authed()) return;
        bool ok = timeSyncNow(config);
        StaticJsonDocument<192> d;
        d["ok"] = ok;
        char now[32]; timeNowString(now, sizeof(now));
        d["now"] = now;
        if (!ok) d["error"] = "NTP did not respond (check server and network)";
        String out; serializeJson(d, out);
        server.send(ok ? 200 : 502, "application/json", out);
    }

    // Existing secrets are never sent to the browser - only whether one is set.
    // The page posts an empty string to mean "leave unchanged", mirroring the
    // serial menu's "********" default.
    void handleGetConfig()
    {
        if (!authed()) return;

        StaticJsonDocument<2048> d;
        JsonObject w = d.createNestedObject("wifi");
        w["ssid"] = config.wifi.ssid;
        w["enabled"] = config.wifi.enabled;
        w["hasPassword"] = strlen(config.wifi.password) > 0;

        JsonObject m = d.createNestedObject("mqtt");
        m["enabled"] = config.mqtt.enabled;
        m["server"] = config.mqtt.server;
        m["port"] = config.mqtt.port;
        m["useTLS"] = config.mqtt.useTLS;
        m["username"] = config.mqtt.username;
        m["hasPassword"] = strlen(config.mqtt.password) > 0;
        m["basePrefix"] = config.mqtt.basePrefix;
        m["topicPrefix"] = config.mqtt.topicPrefix;

        JsonObject l = d.createNestedObject("lora");
        l["frequency"] = config.lora.frequency;
        l["bandwidth"] = config.lora.bandwidth;
        l["spreadingFactor"] = config.lora.spreadingFactor;
        l["codingRate"] = config.lora.codingRate;
        l["txPower"] = config.lora.txPower;

        JsonObject r = d.createNestedObject("repeater");
        r["nodeName"] = config.repeater.nodeName;
        r["maxHops"] = config.repeater.maxHops;

        JsonObject g = d.createNestedObject("log");
        g["enabled"] = config.log.enabled;
        g["server"] = config.log.server;
        g["port"] = config.log.port;
        g["minLevel"] = config.log.minLevel;
        g["heartbeatSec"] = config.log.heartbeatSec;

        webhooksToJson(d.createNestedArray("webhooks"));

        JsonObject clk = d.createNestedObject("clock");
        clk["ntpServer"] = config.clock.ntpServer;
        clk["timezoneMinutes"] = config.clock.timezoneMinutes;
        clk["autoSync"] = config.clock.autoSync;
        clk["valid"] = timeIsValid();
        {
            char now[32]; timeNowString(now, sizeof(now));
            clk["now"] = now;
        }

        JsonObject loc = d.createNestedObject("location");
        loc["latitude"] = config.location.latitude;
        loc["longitude"] = config.location.longitude;

        String out;
        serializeJson(d, out);
        server.send(200, "application/json", out);
    }

    void handleSetConfig()
    {
        if (!authed()) return;

        StaticJsonDocument<2048> d;
        if (deserializeJson(d, server.arg("plain")) != DeserializationError::Ok)
        {
            server.send(400, "application/json", "{\"error\":\"bad json\"}");
            return;
        }

        bool needsRestart = false;

        if (d.containsKey("wifi"))
        {
            JsonObject w = d["wifi"];
            if (w.containsKey("ssid")) copyStr(config.wifi.ssid, sizeof(config.wifi.ssid), w["ssid"]);
            if (w.containsKey("enabled")) config.wifi.enabled = w["enabled"];
            if (!setSecret(config.wifi.password, sizeof(config.wifi.password), w["password"]))
            {
                server.send(400, "application/json",
                            "{\"error\":\"wifi password too long (max 63 characters)\"}");
                return;
            }
            needsRestart = true;
        }
        if (d.containsKey("mqtt"))
        {
            JsonObject m = d["mqtt"];
            if (m.containsKey("enabled")) config.mqtt.enabled = m["enabled"];
            if (m.containsKey("server")) copyStr(config.mqtt.server, sizeof(config.mqtt.server), m["server"]);
            if (m.containsKey("port")) config.mqtt.port = (uint16_t)(int)m["port"];
            if (m.containsKey("useTLS")) config.mqtt.useTLS = m["useTLS"];
            if (m.containsKey("username")) copyStr(config.mqtt.username, sizeof(config.mqtt.username), m["username"]);
            if (!setSecret(config.mqtt.password, sizeof(config.mqtt.password), m["password"]))
            {
                server.send(400, "application/json",
                            "{\"error\":\"mqtt password too long (max 63 characters)\"}");
                return;
            }
            if (m.containsKey("basePrefix"))
            {
                copyStr(config.mqtt.basePrefix, sizeof(config.mqtt.basePrefix), m["basePrefix"]);
                copyStr(config.mqtt.topicPrefix, sizeof(config.mqtt.topicPrefix), m["basePrefix"]);
            }
            needsRestart = true;
        }
        if (d.containsKey("lora"))
        {
            JsonObject l = d["lora"];
            if (l.containsKey("frequency")) config.lora.frequency = l["frequency"];
            if (l.containsKey("bandwidth")) config.lora.bandwidth = l["bandwidth"];
            if (l.containsKey("spreadingFactor")) config.lora.spreadingFactor = clampInt(l["spreadingFactor"], 7, 12);
            if (l.containsKey("codingRate")) config.lora.codingRate = clampInt(l["codingRate"], 5, 8);
            if (l.containsKey("txPower")) config.lora.txPower = clampInt(l["txPower"], 2, 22);
            needsRestart = true;   // setupLoRa() only runs at boot
        }
        if (d.containsKey("repeater"))
        {
            JsonObject r = d["repeater"];
            if (r.containsKey("nodeName"))
            {
                copyStr(config.repeater.nodeName, sizeof(config.repeater.nodeName), r["nodeName"]);
                mesh.setName(config.repeater.nodeName);   // else MeshCore keeps the old name
            }
            if (r.containsKey("maxHops")) config.repeater.maxHops = clampInt(r["maxHops"], 0, 63);
        }
        if (d.containsKey("log"))
        {
            JsonObject g = d["log"];
            if (g.containsKey("enabled")) config.log.enabled = g["enabled"];
            if (g.containsKey("server")) copyStr(config.log.server, sizeof(config.log.server), g["server"]);
            if (g.containsKey("port")) config.log.port = (uint16_t)(int)g["port"];
            if (g.containsKey("minLevel")) config.log.minLevel = clampInt(g["minLevel"], 0, 3);
            if (g.containsKey("heartbeatSec")) config.log.heartbeatSec = clampInt(g["heartbeatSec"], 0, 3600);
            needsRestart = true;
        }
        if (d.containsKey("clock"))
        {
            JsonObject c = d["clock"];
            if (c.containsKey("ntpServer")) copyStr(config.clock.ntpServer, sizeof(config.clock.ntpServer), c["ntpServer"]);
            if (c.containsKey("timezoneMinutes")) config.clock.timezoneMinutes = (int16_t)clampInt(c["timezoneMinutes"], -840, 840);
            if (c.containsKey("autoSync")) config.clock.autoSync = c["autoSync"];
        }
        if (d.containsKey("webhooks"))
        {
            String err;
            if (!applyWebhooks(d["webhooks"].as<JsonArray>(), err))
            {
                server.send(400, "application/json", String("{\"error\":\"") + err + "\"}");
                return;
            }
        }
        if (d.containsKey("location"))
        {
            JsonObject loc = d["location"];
            if (loc.containsKey("latitude")) config.location.latitude = loc["latitude"];
            if (loc.containsKey("longitude")) config.location.longitude = loc["longitude"];
        }

        if (!settings.saveConfig(config))
        {
            server.send(500, "application/json", "{\"error\":\"save failed\"}");
            return;
        }
        server.send(200, "application/json",
                    needsRestart ? "{\"ok\":true,\"restart\":true}" : "{\"ok\":true}");
    }

    void handleRestart()
    {
        if (!authed()) return;
        server.send(200, "application/json", "{\"ok\":true}");
        delay(400);   // let the response flush before we drop the connection
        ESP.restart();
    }

    static void copyStr(char *dest, size_t n, const char *src)
    {
        if (!src) return;
        strncpy(dest, src, n - 1);
        dest[n - 1] = '\0';
    }

    // Empty or absent means "keep the current value". Returns false if the value
    // is too long: truncating a secret silently is worse than refusing it, since
    // the caller would never learn why authentication then fails.
    static bool setSecret(char *dest, size_t n, JsonVariant v)
    {
        if (v.isNull()) return true;
        const char *s = v.as<const char *>();
        if (!s || *s == '\0') return true;
        if (strlen(s) > n - 1) return false;
        strncpy(dest, s, n - 1);
        dest[n - 1] = '\0';
        return true;
    }

    static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    static const char PAGE[] PROGMEM;
};

const char MeshWebUI::PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RAK3112 MeshCore</title><style>
:root{color-scheme:light dark}
body{font:15px/1.5 system-ui,-apple-system,sans-serif;margin:0;padding:1rem;max-width:52rem;margin-inline:auto}
h1{font-size:1.1rem;margin:0 0 .25rem}
.sub{opacity:.65;font-size:.85rem;margin-bottom:1rem}
.card{border:1px solid color-mix(in srgb,currentColor 18%,transparent);border-radius:.6rem;padding:.85rem;margin-bottom:.85rem}
label{display:block;font-size:.8rem;opacity:.7;margin-bottom:.25rem}
select,input,button,textarea{font:inherit;padding:.5rem;border-radius:.4rem;border:1px solid color-mix(in srgb,currentColor 25%,transparent);background:transparent;color:inherit;width:100%;box-sizing:border-box}
button{cursor:pointer;background:color-mix(in srgb,currentColor 10%,transparent);width:auto}
button:hover{background:color-mix(in srgb,currentColor 18%,transparent)}
.row{display:flex;gap:.5rem;align-items:flex-end;flex-wrap:wrap}
.row>*{flex:1 1 12rem}
.row>button{flex:0 0 auto}
ul{list-style:none;padding:0;margin:0}
li{padding:.45rem 0;border-bottom:1px solid color-mix(in srgb,currentColor 10%,transparent)}
li:last-child{border-bottom:0}
.meta{font-size:.75rem;opacity:.6}
.out{opacity:.75}
.tag{font-size:.7rem;border:1px solid currentColor;border-radius:.25rem;padding:0 .3rem;opacity:.7}
.empty{opacity:.5;font-style:italic}
code{font-family:ui-monospace,monospace;font-size:.85em}
fieldset{border:1px solid color-mix(in srgb,currentColor 15%,transparent);border-radius:.4rem;margin:.6rem 0;padding:.6rem}
legend{font-size:.8rem;opacity:.75;padding:0 .3rem}
.warn{font-size:.78rem;border-left:3px solid #d9822b;padding:.4rem .6rem;margin-bottom:.6rem;opacity:.85}
summary::-webkit-details-marker{opacity:.5}
</style></head><body>
<h1>RAK3112 &middot; MeshCore</h1>
<div class="sub" id="ident">loading&hellip;</div>

<div class="card">
  <div class="row">
    <div><label for="to">Send to</label><select id="to"><option value="-1">Public channel</option></select></div>
    <div style="flex:2 1 20rem"><label for="msg">Message</label><input id="msg" placeholder="type a message" maxlength="140"></div>
    <button id="send">Send</button>
  </div>
  <div class="meta" id="status" style="margin-top:.5rem"></div>
</div>

<div class="card">
  <div class="row" style="margin-bottom:.5rem">
    <strong style="flex:1">Contacts</strong>
    <button id="adv" title="Broadcast a signed advert so other nodes can add this gateway">Announce me</button>
  </div>
  <ul id="contacts"><li class="empty">none yet &mdash; contacts appear when a node&rsquo;s advert is received</li></ul>
</div>

<details class="card" id="cfgcard">
  <summary style="cursor:pointer"><strong>Settings</strong> <span class="meta" id="cfgnote"></span></summary>
  <div id="cfgbody" style="margin-top:.75rem">
    <div class="warn">Sent over plain HTTP on your LAN &mdash; readable to anything on the network. Password fields are blank because existing values are never sent to the browser; leave blank to keep them.</div>

    <fieldset><legend>LoRa <span class="meta">restart required</span></legend>
      <div class="row">
        <div><label>Frequency MHz</label><input id="c_freq" type="number" step="0.001"></div>
        <div><label>Bandwidth kHz</label><input id="c_bw" type="number" step="0.1"></div>
        <div><label>Spreading factor</label><input id="c_sf" type="number" min="7" max="12"></div>
        <div><label>Coding rate</label><input id="c_cr" type="number" min="5" max="8"></div>
        <div><label>TX power dBm</label><input id="c_tx" type="number" min="2" max="22"></div>
      </div>
    </fieldset>

    <fieldset><legend>Node <span class="meta">applies immediately</span></legend>
      <div class="row">
        <div><label>Node name</label><input id="c_name"></div>
        <div><label>Max hops <span class="meta">0 = no repeat</span></label><input id="c_hops" type="number" min="0" max="63"></div>
        <div><label>Latitude</label><input id="c_lat" type="number" step="0.000001"></div>
        <div><label>Longitude</label><input id="c_lon" type="number" step="0.000001"></div>
      </div>
    </fieldset>

    <fieldset><legend>WiFi <span class="meta">restart required &mdash; wrong values lock you out</span></legend>
      <div class="row">
        <div><label>SSID</label><input id="c_ssid"></div>
        <div><label>Password <span class="meta" id="c_wpwset"></span></label><input id="c_wpw" type="password" placeholder="unchanged"></div>
      </div>
    </fieldset>

    <fieldset><legend>MQTT <span class="meta">restart required</span></legend>
      <div class="warn">WiFi only runs when MQTT is enabled on this firmware. Turning MQTT off also takes down the web UI, syslog and webhooks, and recovery needs a USB cable.</div>
      <div class="row">
        <div><label><input type="checkbox" id="c_mqen" style="width:auto"> Enabled</label></div>
        <div><label>Broker</label><input id="c_mqsrv"></div>
        <div><label>Port</label><input id="c_mqport" type="number" min="1" max="65535"></div>
        <div><label><input type="checkbox" id="c_mqtls" style="width:auto"> TLS</label></div>
        <div><label>Username</label><input id="c_mquser"></div>
        <div><label>Password <span class="meta" id="c_mqpwset"></span></label><input id="c_mqpw" type="password" placeholder="unchanged"></div>
        <div><label>Topic prefix</label><input id="c_mqpfx"></div>
      </div>
    </fieldset>

    <fieldset><legend>Remote logging <span class="meta">restart required</span></legend>
      <div class="row">
        <div><label><input type="checkbox" id="c_logen" style="width:auto"> Enabled</label></div>
        <div><label>Syslog server</label><input id="c_logsrv"></div>
        <div><label>Port</label><input id="c_logport" type="number" min="1" max="65535"></div>
        <div><label>Min level <span class="meta">0=debug 3=error</span></label><input id="c_loglvl" type="number" min="0" max="3"></div>
        <div><label>Heartbeat s <span class="meta">0 = off</span></label><input id="c_loghb" type="number" min="0" max="3600"></div>
      </div>
    </fieldset>

    <fieldset><legend>Clock / NTP <span class="meta">applies immediately</span></legend>
      <div class="row">
        <div><label><input type="checkbox" id="c_clkauto" style="width:auto"> Sync at boot</label></div>
        <div><label>NTP server</label><input id="c_clkntp" placeholder="pool.ntp.org"></div>
        <div><label>UTC offset (minutes)</label><input id="c_clktz" type="number" min="-840" max="840" step="15"></div>
      </div>
      <div class="row" style="margin-top:.5rem">
        <button id="clksync" type="button">Sync now</button>
        <span class="meta" id="clkstate" style="flex:1"></span>
      </div>
    </fieldset>

    <fieldset><legend>Webhooks <span class="meta">applies immediately</span></legend>
      <div class="warn">Each endpoint gets its own copy of every message and its own token. The token is sent as <code>Authorization: Bearer &lt;token&gt;</code> so your server can verify the POST came from here. You choose the value; it is never readable back.</div>
      <div id="whlist"></div>
      <div class="meta" id="whglobal" style="margin-top:.4rem"></div>
    </fieldset>

    <fieldset><legend>Firmware <span class="meta">reboots on success</span></legend>
      <div class="warn"><strong>Uploading the wrong image can leave the gateway unreachable</strong> and needing a USB cable. The upload goes to the inactive partition and only becomes active once fully verified, so an interrupted transfer is safe — a <em>complete but wrong</em> image is not. Use <code>firmware.bin</code> from <code>.pio/build/rak3112_mqtt/</code>.</div>
      <div class="meta" id="fwinfo">&nbsp;</div>
      <div class="row" style="margin-top:.5rem">
        <div style="flex:2 1 18rem"><label>Firmware image (.bin)</label><input id="fwfile" type="file" accept=".bin"></div>
        <button id="fwup" type="button">Upload &amp; reboot</button>
      </div>
      <div id="fwbar" style="display:none;margin-top:.5rem;height:.5rem;border-radius:.25rem;background:color-mix(in srgb,currentColor 15%,transparent)">
        <div id="fwfill" style="height:100%;width:0;border-radius:.25rem;background:currentColor;opacity:.6"></div>
      </div>
      <div class="meta" id="fwstatus" style="margin-top:.35rem"></div>
    </fieldset>

    <div class="row" style="margin-top:.75rem">
      <button id="cfgsave">Save settings</button>
      <button id="cfgrestart">Restart device</button>
      <span class="meta" id="cfgstatus" style="flex:1"></span>
    </div>
  </div>
</details>

<div class="card">
  <strong>Messages</strong>
  <ul id="msgs" style="margin-top:.5rem"><li class="empty">nothing received yet</li></ul>
</div>

<script>
const $=s=>document.querySelector(s);
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
let lastSel=-1;
async function refresh(){
  let d; try{ d=await (await fetch('/api/state')).json() }catch(e){ return }
  $('#ident').innerHTML='<code>'+esc(d.name)+'</code> &middot; node '+esc(d.self)+' &middot; channel '+esc(d.channel);

  const sel=$('#to'); lastSel=sel.value;
  sel.innerHTML='<option value="-1">Public channel</option>';
  d.contacts.forEach(c=>{
    const o=document.createElement('option'); o.value=c.idx;
    o.textContent=c.name+' ('+c.rssi+' dBm)'; sel.appendChild(o);
  });
  sel.value=lastSel;

  const cl=$('#contacts');
  cl.innerHTML = d.contacts.length ? '' : '<li class="empty">none yet &mdash; contacts appear when a node&rsquo;s advert is received</li>';
  d.contacts.forEach(c=>{
    const li=document.createElement('li');
    li.innerHTML=esc(c.name)+' <span class="meta">'+c.rssi+' dBm</span>';
    cl.appendChild(li);
  });

  const ml=$('#msgs');
  ml.innerHTML = d.messages.length ? '' : '<li class="empty">nothing received yet</li>';
  d.messages.forEach(m=>{
    const li=document.createElement('li');
    if(m.out) li.className='out';
    const tag=m.direct?'<span class="tag">direct</span> ':'';
    const dir=m.out?'&rarr; ':'';
    li.innerHTML=tag+dir+'<strong>'+esc(m.from)+'</strong><br>'+esc(m.text)+
      '<div class="meta">'+(m.out?'sent':(m.rssi+' dBm'))+'</div>';
    ml.appendChild(li);
  });
}
async function post(url,body){
  const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})});
  return r.ok ? null : ((await r.json().catch(()=>({}))).error || ('HTTP '+r.status));
}
$('#send').onclick=async()=>{
  const t=$('#msg').value.trim(); if(!t) return;
  $('#status').textContent='sending&hellip;'; $('#send').disabled=true;
  const err=await post('/api/send',{text:t,to:parseInt($('#to').value,10)});
  $('#send').disabled=false;
  $('#status').textContent = err ? ('failed: '+err) : 'sent';
  if(!err) $('#msg').value='';
  refresh();
};
$('#msg').addEventListener('keydown',e=>{ if(e.key==='Enter') $('#send').click() });
$('#adv').onclick=async()=>{
  $('#status').textContent='announcing&hellip;';
  const err=await post('/api/advert');
  $('#status').textContent = err ? ('advert failed: '+err) : 'advert sent — add this node on your other device';
};

const g=id=>document.getElementById(id);
function fill(c){
  g('c_freq').value=c.lora.frequency; g('c_bw').value=c.lora.bandwidth;
  g('c_sf').value=c.lora.spreadingFactor; g('c_cr').value=c.lora.codingRate;
  g('c_tx').value=c.lora.txPower;
  g('c_name').value=c.repeater.nodeName; g('c_hops').value=c.repeater.maxHops;
  g('c_lat').value=c.location.latitude; g('c_lon').value=c.location.longitude;
  g('c_ssid').value=c.wifi.ssid; g('c_wpwset').textContent=c.wifi.hasPassword?'(set)':'(not set)';
  g('c_mqen').checked=c.mqtt.enabled; mqttWasEnabled=c.mqtt.enabled; g('c_mqsrv').value=c.mqtt.server;
  g('c_mqport').value=c.mqtt.port; g('c_mqtls').checked=c.mqtt.useTLS;
  g('c_mquser').value=c.mqtt.username; g('c_mqpwset').textContent=c.mqtt.hasPassword?'(set)':'(not set)';
  g('c_mqpfx').value=c.mqtt.basePrefix;
  g('c_logen').checked=c.log.enabled; g('c_logsrv').value=c.log.server;
  g('c_logport').value=c.log.port; g('c_loglvl').value=c.log.minLevel;
  g('c_loghb').value=c.log.heartbeatSec;
  renderHooks(c.webhooks||[]);
  const k=c.clock||{};
  g('c_clkauto').checked=!!k.autoSync; g('c_clkntp').value=k.ntpServer||'';
  g('c_clktz').value=k.timezoneMinutes||0;
  g('clkstate').textContent = k.valid ? ('clock set — '+k.now)
      : 'clock NOT set — webhook ts is uptime, not a real time';
                           ' · dropped '+(w.dropped||0)+' · pending '+(w.pending||0);
}
let cfgLoaded=false, lastSavedUrl=null, mqttWasEnabled=false;

// A revealed token still on screen that was never copied is unrecoverable once
// the row is re-rendered, so both the collapse guard and the post-save message
// have to look across every endpoint rather than a single global box.
function hookRows(){ return Array.from(document.querySelectorAll('#whlist > .card')); }
function uncopiedTokens(){
  return hookRows().some(d => d.querySelector('.whReveal').style.display!=='none'
                              && d.dataset.copied!=='1');
}
function markSavedTokens(){
  hookRows().forEach(d=>{
    const rev=d.querySelector('.whReveal');
    if(rev.style.display==='none') return;
    const copied=d.dataset.copied==='1';
    const m=d.querySelector('.whRevMsg');
    m.innerHTML = copied
      ? '<strong>Saved to the gateway.</strong> You copied this token — make sure your endpoint has it, then press Done.'
      : '<strong>Saved — but you have not copied it yet.</strong> Copy it now and paste it into your receiving endpoint. Once you press Done it cannot be shown again.';
    m.style.borderLeftColor = copied ? '#3a9d5d' : '#c0392b';
  });
}
g('cfgcard').addEventListener('toggle',async e=>{
  if(!e.target.open){
    if(uncopiedTokens()){
      alert('A newly generated webhook token is still on screen and has not been copied. It cannot be shown again.');
      e.target.open=true;
    }
    return;
  }
  if(cfgLoaded) return;
  fwInfo();
  try{ fill(await (await fetch('/api/config')).json()); cfgLoaded=true;
       g('cfgnote').textContent=''; }
  catch(err){ g('cfgnote').textContent='failed to load' }
});
g('cfgsave').onclick=async()=>{
  // Saving posts the whole form. If it never loaded, every checkbox reads false
  // and the save would switch off WiFi, MQTT and logging in one click - which is
  // exactly how this device was taken off the network once.
  if(!cfgLoaded){
    g('cfgstatus').textContent='settings have not loaded yet — reopen the panel before saving';
    return;
  }
  const body={
    lora:{frequency:parseFloat(g('c_freq').value),bandwidth:parseFloat(g('c_bw').value),
          spreadingFactor:parseInt(g('c_sf').value,10),codingRate:parseInt(g('c_cr').value,10),
          txPower:parseInt(g('c_tx').value,10)},
    repeater:{nodeName:g('c_name').value,maxHops:parseInt(g('c_hops').value,10)},
    location:{latitude:parseFloat(g('c_lat').value),longitude:parseFloat(g('c_lon').value)},
    wifi:{ssid:g('c_ssid').value,password:g('c_wpw').value},
    mqtt:{enabled:g('c_mqen').checked,server:g('c_mqsrv').value,port:parseInt(g('c_mqport').value,10),
          useTLS:g('c_mqtls').checked,username:g('c_mquser').value,password:g('c_mqpw').value,
          basePrefix:g('c_mqpfx').value},
    log:{enabled:g('c_logen').checked,server:g('c_logsrv').value,port:parseInt(g('c_logport').value,10),
         minLevel:parseInt(g('c_loglvl').value,10),heartbeatSec:parseInt(g('c_loghb').value,10)},
    webhooks:collectHooks(),
    clock:{ntpServer:g('c_clkntp').value,timezoneMinutes:parseInt(g('c_clktz').value,10)||0,
           autoSync:g('c_clkauto').checked},
  };
  if(!g('c_mqen').checked && mqttWasEnabled){
    if(!confirm('Disabling MQTT also stops WiFi on this firmware, which means no web UI, no syslog and no webhooks. You would need a USB cable to undo it.\n\nDisable MQTT anyway?')){
      g('cfgstatus').textContent='cancelled'; return;
    }
  }
  g('cfgstatus').textContent='saving...'; g('cfgsave').disabled=true;
  const err=await post('/api/config',body);
  g('cfgsave').disabled=false;
  g('cfgstatus').textContent = err ? ('failed: '+err)
      : 'saved — restart required for radio, WiFi, MQTT and logging changes';
  if(!err){
    g('c_wpw').value=''; g('c_mqpw').value='';
    document.querySelectorAll('#whlist .whTok').forEach(i=>i.value='');
    cfgLoaded=false;
    // Deliberately NOT clearing revealed tokens here. Saving used to hide them,
    // which meant the gateway held a secret the operator could no longer read -
    // the exact cause of a "token mismatch" that looked like a save failure.
    markSavedTokens();
  }
};
g('clksync').onclick=async()=>{
  g('clkstate').textContent='syncing...';
  const err=await post('/api/timesync');
  if(err){ g('clkstate').textContent='sync failed: '+err; return; }
  try{ fill(await (await fetch('/api/config')).json()); }catch(e){}
};
g('cfgrestart').onclick=async()=>{
  if(!confirm('Restart the gateway now? The page will be unreachable for a few seconds.')) return;
  g('cfgstatus').textContent='restarting...';
  await post('/api/restart');
  setTimeout(()=>{ g('cfgstatus').textContent='restarted — reload the page'; },6000);
};


function hookRow(w){
  const d=document.createElement('div');
  d.className='card'; d.style.margin='.5rem 0';
  d.innerHTML=
    '<div class="row">'+
      '<div><label><input type="checkbox" class="whEn" '+(w.enabled?'checked':'')+' style="width:auto"> Endpoint '+(w.id+1)+'</label></div>'+
      '<div style="flex:2 1 18rem"><label>URL</label><input class="whUrl" placeholder="https://your.host/hook" value="'+esc(w.url||'')+'"></div>'+
      '<div><label>Token <span class="meta whFp">'+(w.hasToken?('set · '+w.tokenLength+' chars · fp '+w.tokenFingerprint):'(not set)')+'</span></label>'+
        '<div style="display:flex;gap:.4rem"><input class="whTok" type="password" placeholder="unchanged" maxlength="128" style="flex:1">'+
        '<button class="whGen" type="button">Generate</button></div></div>'+
      '<div><label><input type="checkbox" class="whPub" '+(w.includePublic!==false?'checked':'')+' style="width:auto"> Public</label></div>'+
      '<div><label><input type="checkbox" class="whDir" '+(w.includeDirect!==false?'checked':'')+' style="width:auto"> Direct</label></div>'+
    '</div>'+
    '<div class="whReveal" style="display:none;margin-top:.5rem">'+
      '<div class="warn whRevMsg"><strong>Copy this now.</strong> It cannot be shown again once dismissed.</div>'+
      '<div style="display:flex;gap:.4rem">'+
        '<input class="whPlain" readonly style="flex:1;font-family:ui-monospace,monospace;font-size:.8rem">'+
        '<button class="whCopy" type="button">Copy</button><button class="whDone" type="button">Done</button>'+
      '</div></div>'+
    '<div class="row" style="margin-top:.4rem">'+
      '<button class="whTest" type="button">Send test delivery</button>'+
      '<span class="meta whStats" style="flex:1">delivered '+(w.delivered||0)+' · failed '+(w.failed||0)+'</span>'+
    '</div>';
  d.dataset.id=w.id; d.dataset.copied='0';
  const q=c=>d.querySelector(c);
  q('.whGen').onclick=()=>{
    if(!(window.crypto&&window.crypto.getRandomValues)){
      g('cfgstatus').textContent='this browser cannot generate securely — use: openssl rand -hex 32'; return; }
    const b=new Uint8Array(32); window.crypto.getRandomValues(b);
    const t=Array.from(b,x=>x.toString(16).padStart(2,'0')).join('');
    q('.whTok').value=t; q('.whPlain').value=t;
    q('.whReveal').style.display='block'; d.dataset.copied='0';
    q('.whRevMsg').innerHTML='<strong>Copy this now.</strong> It cannot be shown again once dismissed.';
    q('.whRevMsg').style.borderLeftColor='';
  };
  q('.whCopy').onclick=async()=>{
    try{ await navigator.clipboard.writeText(q('.whPlain').value); }
    catch(e){ q('.whPlain').select(); }
    d.dataset.copied='1'; g('cfgstatus').textContent='token copied';
  };
  q('.whDone').onclick=()=>{
    if(d.dataset.copied!=='1' && !confirm('You have not copied this token. It cannot be shown again. Hide it anyway?')) return;
    q('.whReveal').style.display='none'; q('.whPlain').value='';
  };
  q('.whTest').onclick=async()=>{
    g('cfgstatus').textContent='saving endpoint '+(w.id+1)+'...';
    const serr=await post('/api/webhooks',{webhooks:[collectHook(d)]});
    if(serr){ g('cfgstatus').textContent='could not save: '+serr; return; }
    q('.whTok').value='';
    g('cfgstatus').textContent='testing endpoint '+(w.id+1)+'...';
    const err=await post('/api/webhooks/test?id='+w.id);
    g('cfgstatus').textContent = err ? ('endpoint '+(w.id+1)+' failed: '+err)
                                     : ('endpoint '+(w.id+1)+' delivered — returned 2xx');
    cfgLoaded=false;
    try{ fill(await (await fetch('/api/config')).json()); cfgLoaded=true; }catch(e){}
  };
  return d;
}
function collectHook(d){
  const q=c=>d.querySelector(c);
  return {id:parseInt(d.dataset.id,10), enabled:q('.whEn').checked, url:q('.whUrl').value,
          token:q('.whTok').value, includePublic:q('.whPub').checked, includeDirect:q('.whDir').checked};
}
function renderHooks(ws){
  // Re-rendering destroys any revealed token, so skip the refresh while one is
  // still on screen uncopied.
  if(uncopiedTokens()) return;
  const box=g('whlist'); box.innerHTML='';
  ws.forEach(w=>box.appendChild(hookRow(w)));
}
function collectHooks(){
  return Array.from(document.querySelectorAll('#whlist > .card')).map(collectHook);
}


async function fwInfo(){
  try{
    const d=await (await fetch('/api/update')).json();
    g('fwinfo').textContent='running '+d.running+' · sketch '+Math.round(d.sketchSize/1024)+
      ' KB · free '+Math.round(d.freeSpace/1024)+' KB'+(d.canUpdate?'':' · NO OTA PARTITION');
  }catch(e){ g('fwinfo').textContent=''; }
}
g('fwup').onclick=()=>{
  const f=g('fwfile').files[0];
  if(!f){ g('fwstatus').textContent='choose a .bin file first'; return; }
  if(!confirm('Upload '+f.name+' ('+Math.round(f.size/1024)+' KB) and reboot?\n\nA wrong image can make the gateway unreachable until you connect a USB cable.')) return;

  const fd=new FormData(); fd.append('firmware',f,f.name);
  const xhr=new XMLHttpRequest();
  xhr.open('POST','/api/update');
  g('fwbar').style.display='block';
  xhr.upload.onprogress=e=>{
    if(!e.lengthComputable) return;
    const pct=Math.round(e.loaded/e.total*100);
    g('fwfill').style.width=pct+'%';
    g('fwstatus').textContent='uploading '+pct+'%';
  };
  xhr.onload=()=>{
    let r={}; try{ r=JSON.parse(xhr.responseText); }catch(e){}
    if(xhr.status===200 && r.ok){
      g('fwstatus').textContent='uploaded '+r.bytes+' bytes — rebooting, reload in ~20s';
      g('fwfill').style.width='100%';
    } else {
      g('fwstatus').textContent='failed: '+(r.error||('HTTP '+xhr.status));
      g('fwfill').style.width='0';
    }
  };
  xhr.onerror=()=>{ g('fwstatus').textContent='upload failed (connection lost)'; };
  g('fwstatus').textContent='starting upload...';
  xhr.send(fd);
};

refresh(); setInterval(refresh,4000);
</script></body></html>)HTML";

#endif // WEB_UI_H
