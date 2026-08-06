#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ctype.h>

// Version
#define FIRMWARE_VERSION "1.0.0"

// EEPROM/NVS Settings
#define CONFIG_NAMESPACE "meshcore_mqtt"
#define CONFIG_MAGIC 0x4D435147  // "MCQG" - MeshCore MQTT Gateway

// Default LoRa Settings
#define DEFAULT_LORA_FREQ 915.8f
#define DEFAULT_LORA_BW 250.0f
#define DEFAULT_LORA_SF 11
#define DEFAULT_LORA_CR 5
#define DEFAULT_LORA_TX_POWER 20

// Default MQTT Settings
#define DEFAULT_MQTT_SERVER "mqtt.example.com"
#define DEFAULT_MQTT_PORT 8883
#define DEFAULT_MQTT_USER ""
#define DEFAULT_MQTT_PASSWORD ""
#define DEFAULT_MQTT_CLIENT_ID "meshcore_gateway"
#define DEFAULT_MQTT_TOPIC_PREFIX "MESHCORE"

// WiFi Settings
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWORD ""

// Pin definitions for common ESP32 LoRa boards
#ifdef HELTEC_V3
    #define LORA_SCK 9
    #define LORA_MISO 11
    #define LORA_MOSI 10
    #define LORA_CS 8
    #define LORA_RST 12
    #define LORA_DIO0 14
    #define LORA_DIO1 -1
    #define LORA_BUSY 13
#elif defined(LILYGO_LORA32_V21)
    // LilyGo LoRa32 V2.1_1.6
    #define LORA_SCK 5
    #define LORA_MISO 19
    #define LORA_MOSI 27
    #define LORA_CS 18
    #define LORA_RST 23
    #define LORA_DIO0 26
    #define LORA_DIO1 33
    #define LORA_DIO2 32
    // Optional OLED display pins
    #define OLED_SDA 21
    #define OLED_SCL 22
    #define OLED_RST 16
#elif defined(HELTEC_V2)
    // Heltec WiFi LoRa 32 V2 (SX1276)
    #define LORA_SCK 5
    #define LORA_MISO 19
    #define LORA_MOSI 27
    #define LORA_CS 18
    #define LORA_RST 14
    #define LORA_DIO0 26
    #define LORA_DIO1 35
    #define LORA_DIO2 34
    // OLED (SSD1306) pins
    #define OLED_SDA 4
    #define OLED_SCL 15
    #define OLED_RST 16
#elif defined(TBEAM_SX1276)
    // TTGO T-Beam (SX1276 variant)
    #define LORA_SCK 5
    #define LORA_MISO 19
    #define LORA_MOSI 27
    #define LORA_CS 18
    #define LORA_RST 23
    #define LORA_DIO0 26
    #define LORA_DIO1 33
    #define LORA_DIO2 32
    // Optional GPS/UART pins exist but not used here
#elif defined(RAK4631_ETH)
    // RAK4631 + WisBlock SX1262 pin mapping (per RAK forum/reference)
    // Note: SX1262 differs from SX127x; RadioLib requires SX1262 class
    #define LORA_SCK   43
    #define LORA_MISO  45
    #define LORA_MOSI  44
    #define LORA_CS    42
    #define LORA_RST   38
    #define LORA_DIO1  47
    #define LORA_BUSY  46
    // Antenna power control (if needed): 37
#else
    // Generic ESP32 with LoRa module
    #define LORA_SCK 5
    #define LORA_MISO 19
    #define LORA_MOSI 27
    #define LORA_CS 18
    #define LORA_RST 14
    #define LORA_DIO0 26
    #define LORA_DIO1 35
    #define LORA_BUSY 32
#endif

// Configuration structures
struct WiFiConfig {
    char ssid[64];
    char password[64];
    bool enabled;
};

struct MQTTConfig {
    char server[128];
    uint16_t port;
    char username[64];
    char password[64];
    char clientId[64];
    char topicPrefix[64];     // effective prefix used for publish/subscribe
    char basePrefix[64];      // e.g. "meshcore"
    char country[32];         // e.g. "Australia"
    char region[32];          // e.g. "NSW" (optional)
    bool useTLS;
    bool insecureTLS;       // Allow TLS without certificate validation (setInsecure)
    bool enabled;
    bool publishRaw;         // Publish raw hex data
    bool publishDecoded;     // Publish decoded messages
    bool subscribeCommands;  // Subscribe to command topics
    bool bridgeAll;          // Subscribe to raw/messages for RF rebroadcast
    bool useCustomCA;        // Use a user-provided CA certificate
    char caCert[2048];       // PEM-encoded CA certificate (optional)
};

struct LoRaConfig {
    float frequency;
    float bandwidth;
    uint8_t spreadingFactor;
    uint8_t codingRate;
    uint8_t txPower;
    uint8_t syncWord;
    bool enableCRC;
};

struct RepeaterConfig {
    char nodeName[32];
    uint32_t nodeId;
    uint8_t maxHops;
    bool autoAck;
    bool broadcastEnabled;
    uint16_t routeTimeout;  // seconds
};

struct SecurityConfig {
    char guestPassword[32];
    char adminPassword[32];
};

// Access control for MeshCore nodes
struct AccessControlConfig {
    bool denyEnabled;            // Enable denylist enforcement
    uint8_t denyCount;           // Number of entries in denylist
    uint32_t denylist[16];       // List of blocked node IDs (hex IDs)
};

struct DiscoveryConfig {
    bool advertEnabled;
    uint16_t advertIntervalSec; // seconds
};

struct LocationConfig {
    float latitude;   // degrees
    float longitude;  // degrees
};

struct ClockConfig {
    char ntpServer[64];
    int16_t timezoneMinutes; // minutes offset from UTC
    bool autoSync;           // sync at boot when WiFi is up
};

// Neighbor tracking for discovery
struct NeighborInfo {
    uint32_t nodeId;
    char nodeName[32];
    int lastRssi;
    float lastSnr;
    float latitude;
    float longitude;
    unsigned long lastSeenMs;
};

struct GatewayConfig {
    uint32_t magic;
    WiFiConfig wifi;
    MQTTConfig mqtt;
    LoRaConfig lora;
    RepeaterConfig repeater;
    SecurityConfig security;
    AccessControlConfig access;
    DiscoveryConfig discovery;
    LocationConfig location;
    ClockConfig clock;
};

// Derive a safe MQTT Client ID from the repeater node name
inline void deriveClientIdFromNodeName(const char* nodeName, char* out, size_t outSize) {
    if (outSize == 0) return;
    if (!nodeName) nodeName = "";
    size_t j = 0;
    for (size_t i = 0; nodeName[i] != '\0' && j < outSize - 1; ++i) {
        char c = nodeName[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_') {
            out[j++] = c;
        } else if (c == ' ' || c == '.' || c == ':' || c == '/' || c == '\\') {
            out[j++] = '_';
        } else {
            // skip any other characters
        }
    }
    if (j == 0) {
        const char* fallback = "MQTT-Gateway";
        for (size_t i = 0; fallback[i] != '\0' && j < outSize - 1; ++i) {
            out[j++] = fallback[i];
        }
    }
    out[j] = '\0';
}

// Build hierarchical topic prefix from base/country/region into out
// Normalizes all segments to uppercase and strips spaces for global consistency
inline void deriveTopicPrefix(const MQTTConfig& mqtt, char* out, size_t outSize) {
    if (outSize == 0) return;
    out[0] = '\0';

    auto appendUpperSegment = [&](const char* seg) {
        if (!seg || seg[0] == '\0') return;
        // Prepare uppercase sanitized copy of the segment
        char upper[64];
        size_t j = 0;
        for (size_t i = 0; seg[i] != '\0' && j < sizeof(upper) - 1; ++i) {
            char c = seg[i];
            if (c == ' ') continue; // drop spaces
            // Allow A-Z, a-z, 0-9, '-', '_'
            if ((c >= 'a' && c <= 'z')) c = (char)toupper((unsigned char)c);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
                upper[j++] = c;
            }
        }
        upper[j] = '\0';
        if (upper[0] == '\0') return;
        if (out[0] != '\0') strncat(out, "/", outSize - strlen(out) - 1);
        strncat(out, upper, outSize - strlen(out) - 1);
    };

    const char* base = (mqtt.basePrefix[0] != '\0') ? mqtt.basePrefix : DEFAULT_MQTT_TOPIC_PREFIX;
    appendUpperSegment(base);
    appendUpperSegment(mqtt.country);   // Expect ISO2 (e.g., AU, NZ)
    appendUpperSegment(mqtt.region);    // Expect subdivision code part (e.g., NSW, AUK)
}

// Default configuration
inline GatewayConfig getDefaultConfig() {
    GatewayConfig config;
    config.magic = CONFIG_MAGIC;
    
    // WiFi defaults
    strncpy(config.wifi.ssid, DEFAULT_WIFI_SSID, sizeof(config.wifi.ssid));
    strncpy(config.wifi.password, DEFAULT_WIFI_PASSWORD, sizeof(config.wifi.password));
    config.wifi.enabled = false;
    
    // MQTT defaults
    strncpy(config.mqtt.server, DEFAULT_MQTT_SERVER, sizeof(config.mqtt.server));
    config.mqtt.port = DEFAULT_MQTT_PORT;
    strncpy(config.mqtt.username, DEFAULT_MQTT_USER, sizeof(config.mqtt.username));
    strncpy(config.mqtt.password, DEFAULT_MQTT_PASSWORD, sizeof(config.mqtt.password));
    strncpy(config.mqtt.clientId, DEFAULT_MQTT_CLIENT_ID, sizeof(config.mqtt.clientId));
    strncpy(config.mqtt.basePrefix, DEFAULT_MQTT_TOPIC_PREFIX, sizeof(config.mqtt.basePrefix));
    config.mqtt.country[0] = '\0';
    config.mqtt.region[0] = '\0';
    deriveTopicPrefix(config.mqtt, config.mqtt.topicPrefix, sizeof(config.mqtt.topicPrefix));
    config.mqtt.useTLS = true;
    config.mqtt.insecureTLS = false;
    config.mqtt.enabled = false;
    config.mqtt.publishRaw = true;
    config.mqtt.publishDecoded = true;
    config.mqtt.subscribeCommands = true;
    config.mqtt.bridgeAll = true;
    config.mqtt.useCustomCA = false;
    config.mqtt.caCert[0] = '\0';
    
    // LoRa defaults
    config.lora.frequency = DEFAULT_LORA_FREQ;
    config.lora.bandwidth = DEFAULT_LORA_BW;
    config.lora.spreadingFactor = DEFAULT_LORA_SF;
    config.lora.codingRate = DEFAULT_LORA_CR;
    config.lora.txPower = DEFAULT_LORA_TX_POWER;
    config.lora.syncWord = 0x12;  // MeshCore default (RADIOLIB_SX126X_SYNC_WORD_PRIVATE)
    config.lora.enableCRC = true;
    
    // Repeater defaults
    strncpy(config.repeater.nodeName, "MQTT-Gateway", sizeof(config.repeater.nodeName));
    config.repeater.nodeId = 0;  // Will be auto-generated
    config.repeater.maxHops = 3;
    config.repeater.autoAck = true;
    config.repeater.broadcastEnabled = true;
    config.repeater.routeTimeout = 300;

    // Security defaults
    config.security.guestPassword[0] = '\0';
    config.security.adminPassword[0] = '\0';

    // Access control defaults
    config.access.denyEnabled = false;
    config.access.denyCount = 0;
    for (size_t i = 0; i < (sizeof(config.access.denylist)/sizeof(config.access.denylist[0])); ++i) {
        config.access.denylist[i] = 0;
    }

    // Discovery defaults
    config.discovery.advertEnabled = false;
    config.discovery.advertIntervalSec = 300; // 5 minutes

    // Location defaults
    config.location.latitude = 0.0f;
    config.location.longitude = 0.0f;

    // Clock defaults
    strncpy(config.clock.ntpServer, "pool.ntp.org", sizeof(config.clock.ntpServer));
    config.clock.timezoneMinutes = 0; // UTC
    config.clock.autoSync = true;
    
    return config;
}

#endif // CONFIG_H

