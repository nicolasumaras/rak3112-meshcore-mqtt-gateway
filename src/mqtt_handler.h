#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#ifdef USE_ETHERNET
#include <Ethernet.h>
#else
#include <WiFi.h>
#include <WiFiClientSecure.h>
#endif
#include <PubSubClient.h>
#include "ca_cert.h"
#include <time.h>
#include <ArduinoJson.h>
#include "config.h"

// Forward declarations
class MQTTHandler;

// Callback types
typedef std::function<void(const uint8_t* payload, size_t length)> MQTTMessageCallback;

class MQTTHandler {
public:
    MQTTHandler(GatewayConfig& cfg) 
        : config(cfg)
#ifdef USE_ETHERNET
        , mqttClient(ethClient)
#else
        , wifiClient(), secureClient(), mqttClient(wifiClient)
#endif
        , lastReconnectAttempt(0), messageCallback(nullptr) {}
    
    bool begin() {
        if (!config.mqtt.enabled) {
            return false;
        }

#ifdef USE_ETHERNET
        // Initialize Ethernet via DHCP; generate a stable locally-administered MAC from nodeId
        byte mac[6];
        mac[0] = 0x02; // locally administered, unicast
        mac[1] = 0x00;
        mac[2] = (byte)((config.repeater.nodeId >> 24) & 0xFF);
        mac[3] = (byte)((config.repeater.nodeId >> 16) & 0xFF);
        mac[4] = (byte)((config.repeater.nodeId >> 8) & 0xFF);
        mac[5] = (byte)(config.repeater.nodeId & 0xFF);
        Ethernet.begin(mac);
        // Force non-TLS for Ethernet path
        config.mqtt.useTLS = false;
#else
        if (!config.wifi.enabled) return false;
        if (!connectWiFi()) return false;
        // Optionally configure TLS
        if (config.mqtt.useTLS) {
            if (config.mqtt.useCustomCA && config.mqtt.caCert[0] != '\0') {
                secureClient.setCACert(config.mqtt.caCert);
            } else {
                secureClient.setCACert(MQTT_CA_CERT);
            }
            if (config.mqtt.insecureTLS) {
                secureClient.setInsecure();
            }
            mqttClient.setClient(secureClient);
        } else {
            mqttClient.setClient(wifiClient);
        }
#endif
        // Prefer hostname; if certificate CN/SAN does not match hostname (common when CN is an IP),
        // we'll retry with the resolved IP address inside connectMQTT().
        mqttClient.setServer(config.mqtt.server, config.mqtt.port);
        mqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
            this->handleMQTTMessage(topic, payload, length);
        });
        mqttClient.setBufferSize(2048);  // Allow larger JSON payloads
        // Improve connection robustness
        mqttClient.setKeepAlive(60);     // Increase keepalive to 60s
        mqttClient.setSocketTimeout(10); // Allow more time for TLS handshake/ops
        
        return connectMQTT();
    }
    
    void loop() {
#ifndef USE_ETHERNET
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("WiFi disconnected, reconnecting..."));
            connectWiFi();
            return;
        }
#endif
        
        // Handle MQTT reconnection
        if (!mqttClient.connected()) {
            unsigned long now = millis();
            if (now - lastReconnectAttempt > 5000) {
                lastReconnectAttempt = now;
                if (connectMQTT()) {
                    lastReconnectAttempt = 0;
                }
            }
        } else {
            mqttClient.loop();
        }
    }
    
    bool isConnected() {
        return mqttClient.connected();
    }
    
    // Publish raw LoRa packet
    void publishRawPacket(const uint8_t* data, size_t length, int rssi, float snr) {
        if (!config.mqtt.publishRaw || !mqttClient.connected()) {
            return;
        }
        
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/raw", config.mqtt.topicPrefix);
        
        // Create JSON payload
        StaticJsonDocument<512> doc;
        doc["timestamp"] = millis();
        doc["rssi"] = rssi;
        doc["snr"] = snr;
        doc["gateway"] = config.mqtt.clientId;
        
        // Convert data to hex string
        char hexStr[length * 2 + 1];
        for (size_t i = 0; i < length; i++) {
            sprintf(&hexStr[i * 2], "%02X", data[i]);
        }
        hexStr[length * 2] = '\0';
        doc["data"] = hexStr;
        doc["length"] = length;
        
        String output;
        serializeJson(doc, output);
        
        mqttClient.publish(topic, output.c_str(), false);
    }
    
    // Publish decoded message
    void publishDecodedMessage(uint32_t fromId, uint32_t toId, const char* message, 
                              uint8_t messageType, int rssi, float snr, uint8_t hopCount) {
        if (!config.mqtt.publishDecoded || !mqttClient.connected()) {
            return;
        }
        
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/messages", config.mqtt.topicPrefix);
        
        StaticJsonDocument<1024> doc;
        doc["timestamp"] = millis();
        doc["from"] = fromId;
        doc["to"] = toId;
        doc["message"] = message;
        doc["type"] = messageType;
        doc["rssi"] = rssi;
        doc["snr"] = snr;
        doc["hops"] = hopCount;
        doc["gateway"] = config.mqtt.clientId;
        
        String output;
        serializeJson(doc, output);
        
        mqttClient.publish(topic, output.c_str(), false);
    }
    
    // Publish node info
    void publishNodeInfo(uint32_t nodeId, const char* nodeName, bool online) {
        if (!mqttClient.connected()) {
            return;
        }
        
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/nodes/%08X", config.mqtt.topicPrefix, nodeId);
        
        StaticJsonDocument<256> doc;
        doc["nodeId"] = nodeId;
        doc["name"] = nodeName;
        doc["online"] = online;
        doc["timestamp"] = millis();
        doc["gateway"] = config.mqtt.clientId;
        
        String output;
        serializeJson(doc, output);
        
        mqttClient.publish(topic, output.c_str(), true);  // Retain node info
    }
    
    // Publish gateway statistics
    void publishStats(uint32_t packetsReceived, uint32_t packetsSent, 
                     uint32_t packetsForwarded, uint32_t packetsFailed) {
        if (!mqttClient.connected()) {
            return;
        }
        
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/gateway/%s/stats", 
                config.mqtt.topicPrefix, config.mqtt.clientId);
        
        StaticJsonDocument<512> doc;
        doc["timestamp"] = millis();
        doc["uptime"] = millis() / 1000;
        doc["packetsReceived"] = packetsReceived;
        doc["packetsSent"] = packetsSent;
        doc["packetsForwarded"] = packetsForwarded;
        doc["packetsFailed"] = packetsFailed;
#ifndef USE_ETHERNET
        doc["rssi"] = WiFi.RSSI();
#else
        doc["rssi"] = 0;
#endif
#ifdef ESP32
        doc["freeHeap"] = ESP.getFreeHeap();
#else
        doc["freeHeap"] = 0;
#endif
        
        String output;
        serializeJson(doc, output);
        
        mqttClient.publish(topic, output.c_str(), false);
    }
    
    // Publish neighbor list
    void publishNeighbors(const NeighborInfo* neighbors, size_t count) {
        if (!mqttClient.connected()) {
            return;
        }
        
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/gateway/%s/neighbors", 
                config.mqtt.topicPrefix, config.mqtt.clientId);
        
        StaticJsonDocument<2048> doc;
        doc["timestamp"] = millis();
        doc["gateway"] = config.mqtt.clientId;
        doc["count"] = count;
        doc["gatewayLat"] = config.location.latitude;
        doc["gatewayLon"] = config.location.longitude;
        
        JsonArray neighborsArray = doc.createNestedArray("neighbors");
        for (size_t i = 0; i < count; i++) {
            JsonObject neighbor = neighborsArray.createNestedObject();
            neighbor["nodeId"] = neighbors[i].nodeId;
            neighbor["name"] = neighbors[i].nodeName;
            neighbor["rssi"] = neighbors[i].lastRssi;
            neighbor["snr"] = neighbors[i].lastSnr;
            neighbor["latitude"] = neighbors[i].latitude;
            neighbor["longitude"] = neighbors[i].longitude;
            neighbor["lastSeen"] = neighbors[i].lastSeenMs;
        }
        
        String output;
        serializeJson(doc, output);
        
        mqttClient.publish(topic, output.c_str(), false);
    }

    // Publish gateway status
    void publishGatewayStatus(bool online) {
        if (!mqttClient.connected()) {
            return;
        }
        
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/gateway/%s/status", 
                config.mqtt.topicPrefix, config.mqtt.clientId);
        
        StaticJsonDocument<384> doc;
        doc["online"] = online;
        doc["timestamp"] = millis();
#ifndef USE_ETHERNET
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
#else
        doc["ip"] = Ethernet.localIP().toString();
        doc["rssi"] = 0;
#endif
        // Include GPS location
        doc["latitude"] = config.location.latitude;
        doc["longitude"] = config.location.longitude;
        
        String output;
        serializeJson(doc, output);
        
        mqttClient.publish(topic, output.c_str(), true);  // Retain status
    }

    // Publish an advert event for visibility/debugging in MQTT
    void publishAdvert(uint32_t nodeId, const char* nodeName, float latitude, float longitude) {
        if (!mqttClient.connected()) {
            return;
        }
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/adverts", config.mqtt.topicPrefix);

        StaticJsonDocument<384> doc;
        doc["timestamp"] = millis();
        doc["nodeId"] = nodeId;
        doc["name"] = nodeName;
        doc["lat"] = latitude;
        doc["lon"] = longitude;
        doc["gateway"] = config.mqtt.clientId;

        String output;
        serializeJson(doc, output);
        mqttClient.publish(topic, output.c_str(), false);
    }
    
    // Set callback for incoming MQTT messages that should be sent to LoRa
    void setMessageCallback(MQTTMessageCallback callback) {
        messageCallback = callback;
    }
    
private:
    GatewayConfig& config;
#ifdef USE_ETHERNET
    EthernetClient ethClient;
#else
    WiFiClient wifiClient;
    WiFiClientSecure secureClient;
#endif
    PubSubClient mqttClient;
    unsigned long lastReconnectAttempt;
    MQTTMessageCallback messageCallback;
    
    bool connectWiFi() {
#ifdef USE_ETHERNET
        return true;
#else
        if (WiFi.status() == WL_CONNECTED) {
            return true;
        }
        
        Serial.print(F("\nConnecting to WiFi: "));
        Serial.println(config.wifi.ssid);
        
        WiFi.mode(WIFI_STA);
        WiFi.begin(config.wifi.ssid, config.wifi.password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) {
            delay(500);
            Serial.print(F("."));
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println(F("\n✓ WiFi connected"));
            Serial.print(F("IP: "));
            Serial.println(WiFi.localIP());
            // Reduce chance of missed MQTT keepalives under load
            WiFi.setSleep(false);
            WiFi.setAutoReconnect(true);

            // Ensure system time is set for TLS validation
            if (config.mqtt.useTLS) {
                setupSystemTimeIfNeeded();
            }
            return true;
        } else {
            Serial.println(F("\n✗ WiFi connection failed"));
            return false;
        }
#endif
    }
    
    bool connectMQTT() {
        if (mqttClient.connected()) {
            return true;
        }
        
        Serial.print(F("Connecting to MQTT: "));
        Serial.println(config.mqtt.server);
        
        // Prepare last will message
        char willTopic[128];
        snprintf(willTopic, sizeof(willTopic), "%s/gateway/%s/status", 
                config.mqtt.topicPrefix, config.mqtt.clientId);
        
        StaticJsonDocument<128> willDoc;
        willDoc["online"] = false;
        willDoc["timestamp"] = millis();
        String willPayload;
        serializeJson(willDoc, willPayload);
        
        bool connected = false;
        if (strlen(config.mqtt.username) > 0) {
            connected = mqttClient.connect(
                config.mqtt.clientId,
                config.mqtt.username,
                config.mqtt.password,
                willTopic,
                1,
                true,
                willPayload.c_str()
            );
        } else {
            connected = mqttClient.connect(
                config.mqtt.clientId,
                willTopic,
                1,
                true,
                willPayload.c_str()
            );
        }
        
        if (connected) {
            Serial.println(F("✓ MQTT connected"));
            
            // Subscribe to command topics including sub-regions when region is empty
            if (config.mqtt.subscribeCommands) {
                char cmdTopic[128];
                snprintf(cmdTopic, sizeof(cmdTopic), "%s/commands/#", config.mqtt.topicPrefix);
                mqttClient.subscribe(cmdTopic);
                Serial.print(F("Subscribed to: "));
                Serial.println(cmdTopic);
                if (config.mqtt.region[0] == '\0') {
                    // When no region is specified, also accept one-level deeper regions
                    char cmdWildcard1[128];
                    snprintf(cmdWildcard1, sizeof(cmdWildcard1), "%s/+/commands/#", config.mqtt.topicPrefix);
                    mqttClient.subscribe(cmdWildcard1);
                    Serial.print(F("Subscribed to: "));
                    Serial.println(cmdWildcard1);
                    // And two-levels deeper (country and region absent)
                    char cmdWildcard2[128];
                    snprintf(cmdWildcard2, sizeof(cmdWildcard2), "%s/+/+/commands/#", config.mqtt.topicPrefix);
                    mqttClient.subscribe(cmdWildcard2);
                    Serial.print(F("Subscribed to: "));
                    Serial.println(cmdWildcard2);
                }
            }
            // Optionally subscribe to bridge topics under hierarchical prefix
            if (config.mqtt.bridgeAll) {
                // Topics that should be bridged over RF
                const char* bridgeTopics[] = { "raw", "messages", "status", "stats", "floods", "adverts" };
                for (size_t i = 0; i < sizeof(bridgeTopics)/sizeof(bridgeTopics[0]); ++i) {
                    char exact[128];
                    snprintf(exact, sizeof(exact), "%s/%s", config.mqtt.topicPrefix, bridgeTopics[i]);
                    mqttClient.subscribe(exact);
                    Serial.print(F("Subscribed to: "));
                    Serial.println(exact);
                    if (config.mqtt.region[0] == '\0') {
                        // parent only: include one and two-level deeper children
                        char oneLevel[128];
                        snprintf(oneLevel, sizeof(oneLevel), "%s/+/%s", config.mqtt.topicPrefix, bridgeTopics[i]);
                        mqttClient.subscribe(oneLevel);
                        Serial.print(F("Subscribed to: "));
                        Serial.println(oneLevel);
                        char twoLevel[128];
                        snprintf(twoLevel, sizeof(twoLevel), "%s/+/+/%s", config.mqtt.topicPrefix, bridgeTopics[i]);
                        mqttClient.subscribe(twoLevel);
                        Serial.print(F("Subscribed to: "));
                        Serial.println(twoLevel);
                    }
                }
            }
            
            // Publish online status
            publishGatewayStatus(true);
            
            return true;
        } else {
            Serial.print(F("✗ MQTT connection failed, rc="));
            Serial.println(mqttClient.state());
            // If TLS is enabled but time might not be set yet, attempt time sync once
            if (config.mqtt.useTLS) {
#ifndef USE_ETHERNET
                setupSystemTimeIfNeeded();
                // Some deployments use a certificate whose CN is the broker IP (not DNS name).
                // Retry once by resolving the hostname and connecting via IP address so hostname
                // verification aligns with the certificate or is omitted by the stack.
                IPAddress brokerIp;
                if (WiFi.hostByName(config.mqtt.server, brokerIp)) {
                    Serial.print(F("Retrying MQTT via resolved IP: "));
                    Serial.println(brokerIp);
                    mqttClient.setServer(brokerIp, config.mqtt.port);
                    bool ipConnected = false;
                    if (strlen(config.mqtt.username) > 0) {
                        ipConnected = mqttClient.connect(
                            config.mqtt.clientId,
                            config.mqtt.username,
                            config.mqtt.password,
                            willTopic,
                            1,
                            true,
                            willPayload.c_str()
                        );
                    } else {
                        ipConnected = mqttClient.connect(
                            config.mqtt.clientId,
                            willTopic,
                            1,
                            true,
                            willPayload.c_str()
                        );
                    }
                    if (ipConnected) {
                        Serial.println(F("✓ MQTT connected via IP"));
                        // Restore hostname for future operations/subscriptions
                        mqttClient.setServer(config.mqtt.server, config.mqtt.port);
                        // Publish online status
                        publishGatewayStatus(true);
                        // Subscribe after connection (re-run minimal subscription for commands)
                        if (config.mqtt.subscribeCommands) {
                            char cmdTopic[128];
                            snprintf(cmdTopic, sizeof(cmdTopic), "%s/commands/#", config.mqtt.topicPrefix);
                            mqttClient.subscribe(cmdTopic);
                            Serial.print(F("Subscribed to: "));
                            Serial.println(cmdTopic);
                        }
                        return true;
                    }
                }
#endif
            }
            return false;
        }
    }
    
    void handleMQTTMessage(char* topic, byte* payload, unsigned int length) {
        Serial.print(F("MQTT message received: "));
        Serial.println(topic);
        
        // Parse topic to determine action
        String topicStr = String(topic);
        String prefix = String(config.mqtt.topicPrefix) + "/commands/";
        String subPrefix = String(config.mqtt.topicPrefix) + "/"; // allows {prefix}/{child}/commands/

        // Accept commands when:
        // 1) Exact: {prefix}/commands/...
        // 2) Parent with one child: {prefix}/{child}/commands/...
        // 3) Parent with two children: {prefix}/{child}/{child}/commands/...
        bool isCommand = false;
        if (topicStr.startsWith(prefix)) {
            isCommand = true;
        } else if (config.mqtt.region[0] == '\0' && topicStr.startsWith(subPrefix)) {
            int firstSlashAfter = topicStr.indexOf('/', subPrefix.length());
            if (firstSlashAfter > 0) {
                // Check for /commands/ at either {prefix}/{x}/commands/ or {prefix}/{x}/{y}/commands/
                int posCommands = topicStr.indexOf("/commands/", subPrefix.length());
                if (posCommands == firstSlashAfter || (posCommands > firstSlashAfter && topicStr.indexOf('/', firstSlashAfter + 1) == posCommands)) {
                    isCommand = true;
                }
            }
        }

        if (isCommand) {
            // Compute command segment after the '/commands/' marker
            int idx = topicStr.indexOf("/commands/");
            String command = topicStr.substring(idx + 10);
            
            if (command == "send" && messageCallback) {
                // Forward message to LoRa via callback
                messageCallback(payload, length);
            } else if (command == "restart") {
                Serial.println(F("Restart command received via MQTT"));
                delay(1000);
                ESP.restart();
            }
        } else if (config.mqtt.bridgeAll && (topicStr == String(config.mqtt.topicPrefix) + "/raw" || topicStr.endsWith("/raw"))) {
            // Expect JSON with { data: hex, gateway?: string }
            StaticJsonDocument<1024> doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err == DeserializationError::Ok && messageCallback) {
                const char* gw = doc["gateway"] | "";
                if (strcmp(gw, config.mqtt.clientId) == 0) {
                    return; // drop self
                }
                const char* hex = doc["data"] | nullptr;
                if (hex) {
                    size_t hexLen = strlen(hex);
                    size_t outLen = hexLen / 2;
                    if (outLen > 0 && outLen <= 512) {
                        uint8_t buf[512];
                        size_t j = 0;
                        for (size_t i = 0; i + 1 < hexLen && j < outLen; i += 2, j++) {
                            char hi = hex[i];
                            char lo = hex[i + 1];
                            uint8_t v = 0;
                            auto cvt = [](char c) -> uint8_t {
                                if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
                                if (c >= 'a' && c <= 'f') return (uint8_t)(10 + c - 'a');
                                if (c >= 'A' && c <= 'F') return (uint8_t)(10 + c - 'A');
                                return 0xFF;
                            };
                            uint8_t vh = cvt(hi);
                            uint8_t vl = cvt(lo);
                            if (vh == 0xFF || vl == 0xFF) { j = 0; break; }
                            v = (uint8_t)((vh << 4) | vl);
                            buf[j] = v;
                        }
                        if (j == outLen) {
                            messageCallback(buf, outLen);
                        }
                    }
                }
            }
        } else if (config.mqtt.bridgeAll && (topicStr == String(config.mqtt.topicPrefix) + "/messages" || topicStr.endsWith("/messages"))) {
            // Expect JSON with { message: string, gateway?: string }
            StaticJsonDocument<1024> doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err == DeserializationError::Ok && messageCallback) {
                const char* gw = doc["gateway"] | "";
                if (strcmp(gw, config.mqtt.clientId) == 0) {
                    return; // drop self
                }
                const char* text = doc["message"] | nullptr;
                if (text) {
                    messageCallback((const uint8_t*)text, strlen(text));
                }
            }
        } else if (config.mqtt.bridgeAll && (topicStr == String(config.mqtt.topicPrefix) + "/adverts" || topicStr.endsWith("/adverts"))) {
            // Expect JSON with { nodeId, name, lat, lon, gateway? }
            StaticJsonDocument<1024> doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err == DeserializationError::Ok && messageCallback) {
                const char* gw = doc["gateway"] | "";
                if (strcmp(gw, config.mqtt.clientId) == 0) {
                    return; // drop self
                }
                uint32_t nodeId = doc["nodeId"] | 0u;
                const char* name = doc["name"] | "";
                double latD = doc["lat"] | 0.0;
                double lonD = doc["lon"] | 0.0;
                // Enforce access control denylist for bridged adverts
                if (config.access.denyEnabled && nodeId != 0) {
                    for (uint8_t i = 0; i < config.access.denyCount && i < (sizeof(config.access.denylist)/sizeof(config.access.denylist[0])); ++i) {
                        if (config.access.denylist[i] == nodeId) {
                            return; // blocked node, do not bridge over RF
                        }
                    }
                }
                // Compose ADVERT line as used on RF
                char advertLine[160];
                snprintf(advertLine, sizeof(advertLine), "ADVERT %08X %s %.6f %.6f",
                         nodeId, name, latD, lonD);
                messageCallback((const uint8_t*)advertLine, strlen(advertLine));
            }
        }
    }

    void setupSystemTimeIfNeeded() {
#ifdef ESP32
        struct tm timeinfo = {};
        if (getLocalTime(&timeinfo)) {
            return;
        }
        Serial.println(F("Setting time via NTP for TLS..."));
        long gmtOffset = (long)config.clock.timezoneMinutes * 60;
        const char* ntp = (config.clock.ntpServer[0] != '\0') ? config.clock.ntpServer : "pool.ntp.org";
        configTime(gmtOffset, 0, ntp);
        const unsigned long start = millis();
        while (!getLocalTime(&timeinfo) && millis() - start < 10000UL) {
            delay(200);
            Serial.print('.');
        }
        if (getLocalTime(&timeinfo)) {
            Serial.println(F("\n✓ Time set"));
        } else {
            Serial.println(F("\n⚠ Failed to set time, TLS may fail"));
        }
#endif
    }
};

#endif // MQTT_HANDLER_H

