/**
 * MeshCore MQTT Gateway
 *
 * This firmware is a LoRa MQTT gateway with serial configuration.
 * It bridges LoRa mesh messages to MQTT brokers.
 *
 * Features:
 * - LoRa packet repeater functionality
 * - MQTT message bridging (LoRa <-> MQTT)
 * - Serial configuration interface
 * - WiFi connectivity
 * - Persistent configuration storage
 *
 * Version: 1.0.0
 */

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <string.h>

// Configuration and handlers
#include "config.h"
#include "settings_manager.h"
#include "mqtt_handler.h"
#include "serial_config.h"
#include "syslog_client.h"
#include "webhook.h"
#ifdef RAK_3112
#include "meshcore_proto.h"
// MeshCore's default "Public" channel PSK, base64 izOH6cXN6mrJ5e26oRXNcg==
static const uint8_t MC_PUBLIC_PSK[16] = {
    0x8B, 0x33, 0x87, 0xE9, 0xC5, 0xCD, 0xEA, 0x6A,
    0xC9, 0xE5, 0xED, 0xBA, 0xA1, 0x15, 0xCD, 0x72};
MeshCoreProto meshProto;
static bool webSendBridge(const uint8_t *data, size_t len);
static void meshRecvBridge(const char *from, const char *text, int rssi, bool isDirect);
#include "web_ui.h"
MeshWebUI *webUI = nullptr;
#endif

// LoRa radio object
#ifdef RAK4631_ETH
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
#elif defined(RAK_3112)
// RadioLib Module signature is (cs, irq, rst, gpio); for SX126x that is
// (NSS, DIO1, RESET, BUSY).
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
#elif defined(HELTEC_V3)
SX1262 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_BUSY);
#else
SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1);
#endif

// UI helpers for consistent boxed output
static const int BOX_CONTENT_WIDTH = 54; // width between bars, excluding leading/trailing single spaces

static void printBoxLine(const String &content)
{
    int pad = BOX_CONTENT_WIDTH - (int)content.length();
    if (pad < 0)
        pad = 0; // truncate visually if too long
    Serial.print("│ ");
    Serial.print(content);
    for (int i = 0; i < pad; ++i)
        Serial.print(' ');
    Serial.println(" │");
}

static void printBoxKeyValue(const char *key, const String &value, int keyWidth = 14)
{
    String line = String(key);
    while ((int)line.length() < keyWidth)
        line += ' ';
    line += value;
    printBoxLine(line);
}

// Global objects
GatewayConfig config;
SettingsManager settingsManager;
MQTTHandler *mqttHandler = nullptr;
ConfigMenu *serialConfig = nullptr;
SyslogClient *sysLog = nullptr;
WebhookSender *webHook = nullptr;
unsigned long lastHeartbeat = 0;

// Statistics
uint32_t packetsReceived = 0;
uint32_t packetsSent = 0;
uint32_t packetsForwarded = 0;
uint32_t packetsFailed = 0;

// Timing
unsigned long lastStatsPublish = 0;
unsigned long lastStatusBlink = 0;
unsigned long lastPacketCheck = 0;
bool configMode = false;

// Radio state
bool radioInitialized = false;
volatile bool packetReceived = false;

// Discovery / Neighbour tracking
static NeighborInfo neighbors[16];
static size_t neighborCount = 0;
static unsigned long lastAdvertSent = 0;

// ---------------------------------------------------------------------------
// MeshCore wire format (see meshcore-dev/MeshCore src/Packet.h, src/Packet.cpp)
//
//   [header:1][transport_codes:4 only if TRANSPORT_*][path_len:1][path:N][payload:rest]
//
//   header  bits 0-1 route type, bits 2-5 payload type, bits 6-7 version
//   path_len  ((hash_size - 1) << 6) | hash_count
// ---------------------------------------------------------------------------
#define MC_ROUTE_MASK             0x03
#define MC_ROUTE_TRANSPORT_FLOOD  0x00
#define MC_ROUTE_FLOOD            0x01
#define MC_ROUTE_DIRECT           0x02
#define MC_ROUTE_TRANSPORT_DIRECT 0x03
#define MC_MAX_PATH_SIZE          64
#define MC_HDR_DO_NOT_RETRANSMIT  0xFF

static inline uint8_t mcRouteType(uint8_t header) { return header & MC_ROUTE_MASK; }
static inline uint8_t mcPayloadType(uint8_t header) { return (header >> 2) & 0x0F; }
static inline bool mcIsFlood(uint8_t header)
{
    uint8_t r = mcRouteType(header);
    return r == MC_ROUTE_FLOOD || r == MC_ROUTE_TRANSPORT_FLOOD;
}
static inline bool mcHasTransportCodes(uint8_t header)
{
    uint8_t r = mcRouteType(header);
    return r == MC_ROUTE_TRANSPORT_FLOOD || r == MC_ROUTE_TRANSPORT_DIRECT;
}
static inline uint8_t mcPathHashSize(uint8_t path_len) { return (path_len >> 6) + 1; }
static inline uint8_t mcPathHashCount(uint8_t path_len) { return path_len & 63; }
static inline bool mcIsValidPathLen(uint8_t path_len)
{
    uint8_t sz = mcPathHashSize(path_len);
    if (sz == 4) return false; // reserved
    return (uint16_t)mcPathHashCount(path_len) * sz <= MC_MAX_PATH_SIZE;
}

// How long a payload stays suppressed. Must comfortably exceed the time for a
// flood to traverse the mesh and come back, or the packet storm returns.
#define MC_DEDUP_WINDOW_MS 60000UL

// Simple recent-packet deduplication to prevent rapid re-repeat loops
static const size_t RECENT_PACKET_SLOTS = 16;
struct RecentPacketEntry
{
    uint32_t hash;
    unsigned long timestampMs;
};
static RecentPacketEntry recentPackets[RECENT_PACKET_SLOTS] = {};

static uint32_t fnv1aHash32(const uint8_t *data, size_t length)
{
    uint32_t hash = 2166136261u; // FNV offset basis
    for (size_t i = 0; i < length; ++i)
    {
        hash ^= data[i];
        hash *= 16777619u; // FNV prime
    }
    return hash;
}

static bool wasPacketSeenRecently(uint32_t hash, unsigned long nowMs, unsigned long windowMs)
{
    for (size_t i = 0; i < RECENT_PACKET_SLOTS; ++i)
    {
        if (recentPackets[i].hash == hash)
        {
            if (nowMs - recentPackets[i].timestampMs <= windowMs)
            {
                return true;
            }
        }
    }
    return false;
}

static void rememberPacket(uint32_t hash, unsigned long nowMs)
{
    // Insert/overwrite the oldest slot
    size_t oldest = 0;
    unsigned long oldestTs = recentPackets[0].timestampMs;
    for (size_t i = 1; i < RECENT_PACKET_SLOTS; ++i)
    {
        if (recentPackets[i].timestampMs < oldestTs)
        {
            oldestTs = recentPackets[i].timestampMs;
            oldest = i;
        }
    }
    recentPackets[oldest].hash = hash;
    recentPackets[oldest].timestampMs = nowMs;
}

// Function declarations
void setupLoRa();
void handleLoRaReceive();
void handleLoRaPacket(uint8_t *data, size_t length, int rssi, float snr);
bool sendLoRaPacket(const uint8_t *data, size_t length);
void meshcoreFloodRepeat(const uint8_t *data, size_t length);
void checkSerialInput();
void publishStats();
void publishNeighbours();
void blinkLED();
void setRadioFlag();
void exitConfigMode();
void sendAdvert();
void printTelemetryToSerial();
void printNeighboursToSerial();

// Radio interrupt flag
volatile uint32_t interruptCount = 0;
void IRAM_ATTR setRadioFlag()
{
    packetReceived = true;
    interruptCount++;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    // Initialize settings manager
    if (!settingsManager.begin())
    {
        Serial.println(F("✗ Failed to initialize settings manager"));
    }

    // Load configuration or use defaults
    if (!settingsManager.loadConfig(config))
    {
        Serial.println(F("⚠ No saved configuration found, using defaults"));
        config = getDefaultConfig();
        settingsManager.saveConfig(config);
    }
    else
    {
        Serial.println(F("✓ Configuration loaded"));
    }

    // Generate node ID if not set
    if (config.repeater.nodeId == 0)
    {
        uint64_t chipid = ESP.getEfuseMac();
        config.repeater.nodeId = (uint32_t)(chipid & 0xFFFFFFFF);
        settingsManager.saveConfig(config);
        Serial.printf("✓ Generated Node ID: 0x%08X\n", config.repeater.nodeId);
    }

    Serial.println();

    // Sync MQTT Client ID with repeater node name
    {
        char prevId[sizeof(config.mqtt.clientId)];
        strncpy(prevId, config.mqtt.clientId, sizeof(prevId));
        prevId[sizeof(prevId) - 1] = '\0';
        deriveClientIdFromNodeName(config.repeater.nodeName, config.mqtt.clientId, sizeof(config.mqtt.clientId));
        if (strcmp(prevId, config.mqtt.clientId) != 0)
        {
            settingsManager.saveConfig(config);
            Serial.printf("✓ MQTT Client ID set to: %s\n", config.mqtt.clientId);
        }
    }

    Serial.println(F("┌────────────────────────────────────────────────────────┐"));
    printBoxLine(String("Node Name: ") + config.repeater.nodeName);
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%08X", config.repeater.nodeId);
        printBoxLine(String("Node ID:   ") + buf);
    }
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f MHz", config.lora.frequency);
        printBoxLine(String("LoRa Freq: ") + buf);
    }
    printBoxLine(String("WiFi:      ") + (config.wifi.enabled ? "Enabled" : "Disabled"));
    printBoxLine(String("MQTT:      ") + (config.mqtt.enabled ? "Enabled" : "Disabled"));
    Serial.println(F("└────────────────────────────────────────────────────────┘"));
    Serial.println();

    // Setup LoRa
    Serial.println(F("Initializing LoRa..."));
    setupLoRa();
    Serial.println(F("✓ LoRa initialized"));

    // Setup MQTT if enabled
    if (config.wifi.enabled && config.mqtt.enabled)
    {
        Serial.println(F("\nInitializing MQTT..."));
        mqttHandler = new MQTTHandler(config);

        // Set callback for MQTT -> LoRa messages
        mqttHandler->setMessageCallback([](const uint8_t *payload, size_t length)
                                        {
            Serial.printf("Forwarding MQTT message to LoRa (%d bytes)\n", length);
            sendLoRaPacket(payload, length); });

        if (mqttHandler->begin())
        {
            Serial.println(F("✓ MQTT initialized"));
            mqttHandler->publishGatewayStatus(true);
        }
        else
        {
            Serial.println(F("✗ MQTT initialization failed"));
        }
    }
    else
    {
        Serial.println(F("⚠ MQTT disabled (WiFi or MQTT not enabled in config)"));
    }

    // Setup serial configuration interface
    serialConfig = new ConfigMenu(config, settingsManager);
    serialConfig->setOnExitCallback(exitConfigMode);
    serialConfig->begin();

    webHook = new WebhookSender(config);
    webHook->setLogger([](const char *m) { if (sysLog) sysLog->log(LOG_INFO, m); });

    // Remote syslog, if an operator configured a collector.
    sysLog = new SyslogClient(config);
    sysLog->begin();
    if (sysLog->isReady())
    {
        Serial.printf("✓ Remote logging -> %s:%u\n", config.log.server, config.log.port);
        sysLog->logf(LOG_INFO, "boot fw=%s node=0x%08X freq=%.3f sf=%u cr=%u bw=%.1f",
                     FIRMWARE_VERSION, config.repeater.nodeId, config.lora.frequency,
                     config.lora.spreadingFactor, config.lora.codingRate, config.lora.bandwidth);
    }

#ifdef RAK_3112
    // MeshCore identity + public channel. The keypair is generated once and
    // persisted, so the node keeps a stable identity across reboots.
    meshProto.begin(config.repeater.nodeName);
    meshProto.setChannelPsk(MC_PUBLIC_PSK, sizeof(MC_PUBLIC_PSK));
    meshProto.setSender(webSendBridge);   // so received direct messages get ACKed
    meshProto.setReceiver(meshRecvBridge);
    Serial.printf("  MeshCore public channel hash: 0x%02X\n", meshProto.channelHash());

    // Web UI needs WiFi, which only comes up as part of the MQTT handler.
    if (config.wifi.enabled && WiFi.status() == WL_CONNECTED)
    {
        webUI = new MeshWebUI(config, settingsManager, meshProto, *webHook, webSendBridge);
        if (webUI->begin())
        {
            Serial.print(F("✓ MeshCore web UI: http://"));
            Serial.println(WiFi.localIP());
            Serial.println(F("  user 'admin', password = admin password (menu 10)"));
        }
    }
    else
    {
        Serial.println(F("⚠ Web UI not started: WiFi is not connected."));
    }
#endif

    Serial.println();
    Serial.println(F("════════════════════════════════════════════════════════"));
    Serial.println(F("✓ Gateway started successfully!"));
    Serial.println(F("════════════════════════════════════════════════════════"));
    Serial.println();
    Serial.println(F("Commands:"));
    Serial.println(F("  'c' - Enter configuration menu"));
    Serial.println(F("  's' - Show statistics"));
    Serial.println(F("  'n' - Show neighbours"));
    Serial.println(F("  'd' - Debug info (interrupt count)"));
    Serial.println(F("  't' - Send test packet (TX test)"));
    Serial.println(F("  'r' - Restart device"));
    Serial.println();
    Serial.println(F("(Hint) Press 'c' at any time to open the configuration menu"));
}

void loop()
{
    // Handle LoRa messages
    if (!configMode)
    {
        handleLoRaReceive();

        // Handle MQTT
        if (mqttHandler)
        {
            mqttHandler->loop();
        }
    }

    // Check for serial commands
#ifdef RAK_3112
    if (webUI) webUI->loop();
    if (webHook) webHook->loop();
#endif

    if (!configMode)
    {
        checkSerialInput();
    }
    else
    {
        serialConfig->handleMenu();
    }

    // Publish statistics periodically
    unsigned long now = millis();
    if (sysLog && sysLog->isReady() && config.log.heartbeatSec > 0 &&
        now - lastHeartbeat > (unsigned long)config.log.heartbeatSec * 1000UL)
    {
        lastHeartbeat = now;
#ifdef RAK_3112
        sysLog->heartbeat(packetsReceived, packetsSent, packetsForwarded, packetsFailed,
                          meshProto.contactCount);
#else
        sysLog->heartbeat(packetsReceived, packetsSent, packetsForwarded, packetsFailed, 0);
#endif
    }

    if (!configMode && mqttHandler && mqttHandler->isConnected() && now - lastStatsPublish > 60000)
    {
        publishStats();
        publishNeighbours(); // Also publish neighbor list with stats
        lastStatsPublish = now;
    }

    // Periodic advert broadcast
    if (!configMode && config.discovery.advertEnabled && now - lastAdvertSent > (unsigned long)config.discovery.advertIntervalSec * 1000UL)
    {
        sendAdvert();
        lastAdvertSent = now;
    }

    // Blink status LED
    if (now - lastStatusBlink > 1000)
    {
        blinkLED();
        lastStatusBlink = now;
    }

    yield();
}

void setupLoRa()
{
    // Initialize SPI
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

    Serial.print(F("Initializing radio... "));

    // Initialize radio with configuration
    int state;
#ifdef RAK4631_ETH
    // SX1262 API differs: frequency, bandwidth (kHz), spreading factor, coding rate, syncWord, power, preambleLength
    state = radio.begin(
        config.lora.frequency,
        config.lora.bandwidth,
        config.lora.spreadingFactor,
        config.lora.codingRate,
        config.lora.syncWord,
        config.lora.txPower,
        8);
    // Enable DIO2 RF switch control and DIO3 TCXO if needed (defaults okay for WisBlock)
    radio.setDio2AsRfSwitch(true);
#elif defined(RAK_3112)
    // RAK3112 (ESP32-S3 + SX1262). Settings mirror meshcore-dev/MeshCore's
    // rak3112 variant. TCXO voltage is passed via begin() rather than a separate
    // setTCXO() call, matching the HELTEC_V3 path below.
    //
    // SX1262 maximum is 22 dBm; the shared config has no clamp of its own and the
    // serial menu does not enforce its advertised 2-20 range, so clamp here.
    {
        int txPower = config.lora.txPower;
        if (txPower > RAK3112_MAX_TX_POWER)
        {
            Serial.printf("\n  TX power %d dBm exceeds SX1262 max, clamping to %d dBm... ",
                          txPower, RAK3112_MAX_TX_POWER);
            txPower = RAK3112_MAX_TX_POWER;
        }
        // Preamble 16, not the 8 the other boards here use: meshcore-dev/MeshCore
        // initialises SX126x with preamble 16 (CustomSX1262.h), and its
        // RadioLibWrappers::preambleLengthForSF() yields 32 for SF<=8 else 16.
        // Matching it is required to interoperate with real MeshCore nodes.
        uint16_t preamble = (config.lora.spreadingFactor <= 8) ? 32 : 16;
        state = radio.begin(
            config.lora.frequency,
            config.lora.bandwidth,
            config.lora.spreadingFactor,
            config.lora.codingRate,
            config.lora.syncWord,
            txPower,
            preamble,
            1.8F,   // DIO3 TCXO voltage
            false); // useRegulatorLDO = false -> DC-DC
        Serial.printf("\n  (MeshCore-compatible preamble: %u) ", preamble);
    }
    if (state == RADIOLIB_ERR_NONE)
    {
        radio.setDio2AsRfSwitch(true);
        radio.setCurrentLimit(140.0);
        radio.setRxBoostedGainMode(true);
    }
#elif defined(HELTEC_V3)
    state = radio.begin(
        config.lora.frequency,
        config.lora.bandwidth,
        config.lora.spreadingFactor,
        config.lora.codingRate,
        config.lora.syncWord,
        config.lora.txPower,
        8, 1.8F, false);
#else
    state = radio.begin(
        config.lora.frequency,
        config.lora.bandwidth,
        config.lora.spreadingFactor,
        config.lora.codingRate,
        config.lora.syncWord,
        config.lora.txPower,
        8, // preamble length
        0  // gain (0 = auto)
    );
#endif

    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("success!"));

        // Enable CRC if configured
        if (config.lora.enableCRC)
        {
            radio.setCRC(true);
        }

// CRITICAL: For LilyGo boards, explicitly set output power and PA config
// This ensures the PA (Power Amplifier) is actually enabled
#if defined(LILYGO_LORA32_V21)
        Serial.print(F("Configuring PA... "));
        // Use PA_BOOST pin (required for LilyGo V2.1)
        state = radio.setOutputPower(config.lora.txPower, true); // true = use PA_BOOST
        if (state == RADIOLIB_ERR_NONE)
        {
            Serial.println(F("OK"));
        }
        else
        {
            Serial.printf("FAILED (%d)\n", state);
        }
#endif

        // CRITICAL: Set packet received action (RadioLib's proper method)
        Serial.print(F("Setting packet received action... "));
        radio.setPacketReceivedAction(setRadioFlag);
        Serial.println(F("OK"));

        // Start continuous listening
        state = radio.startReceive();
        if (state == RADIOLIB_ERR_NONE)
        {
            Serial.println(F("✓ Radio listening for packets"));
            radioInitialized = true;
        }
        else
        {
            if (sysLog) sysLog->logf(LOG_ERROR, "startReceive failed code=%d", state);
            Serial.print(F("✗ Failed to start receive, code: "));
            Serial.println(state);
        }
    }
    else
    {
        Serial.print(F("failed, code: "));
        Serial.println(state);
        Serial.println(F("✗ Check wiring and antenna!"));
    }

    // Print radio configuration
    Serial.println(F("\n┌── LoRa Configuration ──────────────────────────────────┐"));
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f MHz", config.lora.frequency);
        printBoxKeyValue("Frequency:", buf, 16);
    }
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f kHz", config.lora.bandwidth);
        printBoxKeyValue("Bandwidth:", buf, 16);
    }
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", config.lora.spreadingFactor);
        printBoxKeyValue("Spreading Factor:", buf, 18);
    }
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "4/%d", config.lora.codingRate);
        printBoxKeyValue("Coding Rate:", buf, 16);
    }
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d dBm", config.lora.txPower);
        printBoxKeyValue("TX Power:", buf, 16);
    }
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%02X", config.lora.syncWord);
        printBoxKeyValue("Sync Word:", buf, 16);
    }
    printBoxKeyValue("CRC:", String(config.lora.enableCRC ? "Enabled" : "Disabled"), 16);
    Serial.println(F("└────────────────────────────────────────────────────────┘\n"));
}

void handleLoRaReceive()
{
    if (!radioInitialized)
        return;

    // Check if packet was received
    if (packetReceived)
    {
        packetReceived = false;
        Serial.println(F("🔔 Interrupt fired! Reading packet..."));

        // Buffer for received data
        uint8_t buffer[256];

        // Read received data
        int state = radio.readData(buffer, sizeof(buffer));

        if (state == RADIOLIB_ERR_NONE)
        {
            // Get packet info
            size_t length = radio.getPacketLength();
            int rssi = radio.getRSSI();
            float snr = radio.getSNR();

            // Defence in depth against spurious interrupts (see sendLoRaPacket).
            // A zero-length read is never a real packet, and passing it through
            // would inflate packetsReceived and publish an empty payload to MQTT:
            // handleLoRaPacket() has no length guard of its own.
            if (length == 0)
            {
                Serial.println(F("⚠ Spurious interrupt (0 bytes) - ignoring"));
            }
            else
            {
                Serial.printf("📥 RX SUCCESS: %d bytes, RSSI=%d dBm, SNR=%.1f dB\n", length, rssi, snr);
                packetsReceived++;

                // Handle the packet
                handleLoRaPacket(buffer, length, rssi, snr);
            }
        }
        else if (state == RADIOLIB_ERR_CRC_MISMATCH)
        {
            Serial.println(F("⚠ CRC error!"));
        }
        else
        {
            Serial.printf("⚠ Read error, code: %d\n", state);
        }

        // ✅ CRITICAL FIX: Put radio back into receive mode
        state = radio.startReceive();
        if (state != RADIOLIB_ERR_NONE)
        {
            Serial.print(F("✗ Failed to restart receive, code: "));
            Serial.println(state);
            radioInitialized = false;
        }
    }
}

void handleLoRaPacket(uint8_t *data, size_t length, int rssi, float snr)
{
    // Log to serial
    Serial.printf("\n📡 LoRa RX: %d bytes | RSSI: %d dBm | SNR: %.1f dB\n", length, rssi, snr);
    if (sysLog) sysLog->logf(LOG_DEBUG, "rx bytes=%u rssi=%d snr=%.1f type=0x%02X",
                             (unsigned)length, rssi, snr, length ? data[0] : 0);

#ifdef RAK_3112
    // Decode as MeshCore: records contacts from adverts and decrypts messages
    // addressed to us or to the public channel. Non-MeshCore traffic just
    // returns false and falls through to the existing raw handling below.
    meshProto.handleFrame(data, length, rssi);
#endif

    // Print hex dump (first 32 bytes)
    Serial.print("   Data: ");
    for (size_t i = 0; i < min(length, (size_t)32); i++)
    {
        Serial.printf("%02X ", data[i]);
    }
    if (length > 32)
        Serial.print("...");
    Serial.println();

    // Try to interpret as text if printable
    bool isPrintable = true;
    // Track parsed ADVERT details if present so we can publish origin and metadata
    bool parsedAdvert = false;
    uint32_t advertNodeId = 0;
    char advertName[32] = {0};
    float advertLat = 0.0f;
    float advertLon = 0.0f;
    for (size_t i = 0; i < length; i++)
    {
        if (data[i] < 32 || data[i] > 126)
        {
            isPrintable = false;
            break;
        }
    }

    if (isPrintable && length > 0)
    {
        Serial.print("   Text: \"");
        for (size_t i = 0; i < length; i++)
        {
            Serial.write(data[i]);
        }
        Serial.println("\"");

        // Simple neighbour discovery on ADVERT messages: "ADVERT <nodeIdHex> <nodeName> <lat> <lon>"
        if (length >= 6 && strncmp((const char *)data, "ADVERT", 6) == 0)
        {
            char buf[256];
            size_t copyLen = min(length, sizeof(buf) - 1);
            memcpy(buf, data, copyLen);
            buf[copyLen] = '\0';
            char *saveptr;
            char *tok = strtok_r(buf, " ", &saveptr); // ADVERT
            tok = strtok_r(nullptr, " ", &saveptr);   // nodeIdHex
            uint32_t nid = 0;
            if (tok)
            {
                nid = (uint32_t)strtoul(tok, nullptr, 16);
            }
            tok = strtok_r(nullptr, " ", &saveptr); // nodeName
            char nname[32] = {0};
            if (tok)
            {
                strncpy(nname, tok, sizeof(nname) - 1);
            }
            tok = strtok_r(nullptr, " ", &saveptr); // lat
            float lat = tok ? atof(tok) : 0.0f;
            tok = strtok_r(nullptr, " ", &saveptr); // lon
            float lon = tok ? atof(tok) : 0.0f;

            // Apply access control denylist: drop adverts from denied node IDs
            bool denied = false;
            if (config.access.denyEnabled)
            {
                for (uint8_t i = 0; i < config.access.denyCount && i < (sizeof(config.access.denylist) / sizeof(config.access.denylist[0])); ++i)
                {
                    if (config.access.denylist[i] == nid && nid != 0)
                    {
                        denied = true;
                        break;
                    }
                }
            }

            if (denied)
            {
                Serial.println(F("   ✗ Advert dropped (denied node)"));
                // Skip neighbor update and further processing for denied node
                return;
            }

            // Remember parsed advert for later MQTT publication/decoded origin
            parsedAdvert = true;
            advertNodeId = nid;
            strncpy(advertName, nname, sizeof(advertName) - 1);
            advertLat = lat;
            advertLon = lon;

            size_t idx = neighborCount;
            for (size_t i = 0; i < neighborCount; ++i)
            {
                if (neighbors[i].nodeId == nid)
                {
                    idx = i;
                    break;
                }
            }
            if (idx == neighborCount && neighborCount < (sizeof(neighbors) / sizeof(neighbors[0])))
            {
                neighborCount++;
            }
            if (idx < (sizeof(neighbors) / sizeof(neighbors[0])))
            {
                neighbors[idx].nodeId = nid;
                strncpy(neighbors[idx].nodeName, nname, sizeof(neighbors[idx].nodeName) - 1);
                neighbors[idx].nodeName[sizeof(neighbors[idx].nodeName) - 1] = '\0';
                neighbors[idx].lastRssi = rssi;
                neighbors[idx].lastSnr = snr;
                neighbors[idx].latitude = lat;
                neighbors[idx].longitude = lon;
                neighbors[idx].lastSeenMs = millis();
                Serial.println(F("   ✓ Neighbour updated from advert"));
            }
        }
    }

    // Forward to MQTT if connected
    if (mqttHandler && mqttHandler->isConnected())
    {
        // If this was an ADVERT received over RF, publish a structured advert event
        if (parsedAdvert)
        {
            mqttHandler->publishAdvert(advertNodeId, advertName, advertLat, advertLon);
        }
        // Publish raw packet
        if (config.mqtt.publishRaw)
        {
            mqttHandler->publishRawPacket(data, length, rssi, snr);
        }

        // Publish decoded message if it looks like text
        if (config.mqtt.publishDecoded && isPrintable)
        {
            char message[256] = {0};
            size_t msgLen = min(length, sizeof(message) - 1);
            memcpy(message, data, msgLen);

            // For ADVERT messages, set origin to the advertising node; otherwise use gateway id
            uint32_t fromId = parsedAdvert && advertNodeId != 0 ? advertNodeId : config.repeater.nodeId;
            mqttHandler->publishDecodedMessage(
                fromId,
                0xFFFFFFFF, // to (broadcast)
                message,
                0, // message type
                rssi,
                snr,
                0 // hop count
            );
        }

        packetsForwarded++;
    }

    // MeshCore-aware flood repeat. maxHops == 0 disables retransmission entirely.
    if (config.repeater.maxHops > 0 && length > 0)
    {
        meshcoreFloodRepeat(data, length);
    }
}

// Retransmit a MeshCore FLOOD packet the way a real MeshCore repeater does:
// append this node's path hash and increment the hash count, so that downstream
// nodes can detect loops and so the originator learns a return path.
//
// Mirrors mesh::Mesh::routeRecvPacket() in meshcore-dev/MeshCore (src/Mesh.cpp).
//
// Deliberately limited to FLOOD packets. DIRECT routing requires us to match our
// own identity hash against path[0] and shuffle the path, which in turn requires
// a real MeshCore Identity keypair that this firmware does not have. Forwarding
// DIRECT packets without that would corrupt routing, so we leave them alone.
void meshcoreFloodRepeat(const uint8_t *data, size_t length)
{
    uint8_t header = data[0];

    if (header == MC_HDR_DO_NOT_RETRANSMIT)
    {
        Serial.println(F("   ↻ Not repeated (marked do-not-retransmit)"));
        return;
    }
    if (!mcIsFlood(header))
    {
        Serial.printf("   ↻ Not repeated (route type %u is not FLOOD)\n", mcRouteType(header));
        return;
    }

    size_t off = 1;
    if (mcHasTransportCodes(header)) off += 4;
    if (length < off + 1)
    {
        Serial.println(F("   ↻ Not repeated (packet too short for header)"));
        return;
    }

    uint8_t pathLen = data[off];
    if (!mcIsValidPathLen(pathLen))
    {
        Serial.println(F("   ↻ Not repeated (invalid path_len encoding)"));
        return;
    }

    uint8_t hashSize = mcPathHashSize(pathLen);
    uint8_t hashCount = mcPathHashCount(pathLen);
    size_t pathBytes = (size_t)hashCount * hashSize;
    size_t payloadStart = off + 1 + pathBytes;
    if (payloadStart >= length)
    {
        Serial.println(F("   ↻ Not repeated (no payload)"));
        return;
    }

    // Honour the configured hop limit.
    if (hashCount >= config.repeater.maxHops)
    {
        Serial.printf("   ↻ Not repeated (hop limit reached: %u/%u)\n", hashCount, config.repeater.maxHops);
        return;
    }
    if ((size_t)(hashCount + 1) * hashSize > MC_MAX_PATH_SIZE)
    {
        Serial.println(F("   ↻ Not repeated (path full)"));
        return;
    }

    // Deduplicate on payload type + payload only, never on the path. MeshCore
    // does the same (Packet::calculatePacketHash), because the path changes at
    // every hop - hashing the whole frame would make each re-flood look new and
    // defeat loop suppression entirely.
    const uint8_t *payload = data + payloadStart;
    size_t payloadLen = length - payloadStart;
    uint8_t ptype = mcPayloadType(header);
    uint32_t h = fnv1aHash32(&ptype, 1);
    h ^= fnv1aHash32(payload, payloadLen);

    unsigned long nowMs = millis();
    if (wasPacketSeenRecently(h, nowMs, MC_DEDUP_WINDOW_MS))
    {
        Serial.println(F("   ↻ Skipped repeat (already seen this payload)"));
        return;
    }

    // Rebuild the frame with our hash appended to the path.
    uint8_t out[256];
    size_t o = 0;
    out[o++] = header;
    if (mcHasTransportCodes(header))
    {
        memcpy(&out[o], &data[1], 4);
        o += 4;
    }
    out[o++] = (uint8_t)(((hashSize - 1) << 6) | (hashCount + 1));
    memcpy(&out[o], &data[off + 1], pathBytes);
    o += pathBytes;

    // Our path hash. A real MeshCore node uses its Identity public-key hash; we
    // have no keypair, so derive a stable value from the node ID. That is enough
    // for loop bounding and breadcrumbs, but see the DIRECT caveat above.
    for (uint8_t i = 0; i < hashSize; ++i)
    {
        out[o++] = (uint8_t)((config.repeater.nodeId >> (8 * (i % 4))) & 0xFF);
    }

    if (o + payloadLen > sizeof(out))
    {
        Serial.println(F("   ↻ Not repeated (would exceed MTU)"));
        return;
    }
    memcpy(&out[o], payload, payloadLen);
    o += payloadLen;

    // Randomised backoff, lower priority the further the packet has travelled.
    // MeshCore scales this by airtime; a bounded random delay is enough here.
    delay(random(120, 400) * (hashCount + 1));

    // NB: not counted in packetsForwarded - that counter already means
    // "forwarded to MQTT". Repeats show up in packetsSent.
    if (sendLoRaPacket(out, o))
    {
        rememberPacket(h, millis());
        Serial.printf("   ↻ Repeated as FLOOD (path %u -> %u, %u bytes)\n",
                      hashCount, hashCount + 1, (unsigned)o);
    }
}

bool sendLoRaPacket(const uint8_t *data, size_t length)
{
    if (!radioInitialized || length == 0 || length > 255)
    {
        packetsFailed++;
        return false;
    }

    Serial.printf("\n📤 LoRa TX: %d bytes\n", length);

    // Transmit the packet
    int state = radio.transmit((uint8_t *)data, length);

    // radio.transmit() is blocking, and on completion TxDone asserts the same DIO
    // line that setPacketReceivedAction() hooks. That fires the ISR and leaves
    // packetReceived set, so without this the next handleLoRaReceive() reads a
    // stale flag and reports a phantom zero-byte packet. Clear it before
    // returning to RX. Observed on RAK3112 hardware; applies to any board here.
    packetReceived = false;

    if (state == RADIOLIB_ERR_NONE)
    {
        packetsSent++;
        Serial.println("   ✓ Sent successfully");

        // Put radio back into receive mode
        radio.startReceive();
        return true;
    }
    else
    {
        packetsFailed++;
        if (sysLog) sysLog->logf(LOG_ERROR, "tx failed code=%d len=%u", state, (unsigned)length);
        Serial.print("   ✗ Failed, code: ");
        Serial.println(state);

        // Try to recover
        radio.startReceive();
        return false;
    }
}

#ifdef RAK_3112
// Lets the web UI transmit without knowing anything about the radio layer.
static bool webSendBridge(const uint8_t *data, size_t len)
{
    return sendLoRaPacket(data, len);
}

// Fan out decoded inbound messages. Queued, never posted inline - see webhook.h.
static void meshRecvBridge(const char *from, const char *text, int rssi, bool isDirect)
{
    if (webHook) webHook->enqueue(from, text, rssi, isDirect);
    if (sysLog) sysLog->logf(LOG_INFO, "msg %s from=%s rssi=%d",
                             isDirect ? "direct" : "public", from, rssi);
}
#endif

void checkSerialInput()
{
    if (Serial.available())
    {
        char c = Serial.read();

        switch (c)
        {
        case 'c':
        case 'C':
            configMode = true;
            serialConfig->showMainMenu();
            break;

        case 's':
        case 'S':
            Serial.println(F("\n┌── Gateway Statistics ──────────────────────────────────┐"));
            printTelemetryToSerial();
            Serial.println(F("└────────────────────────────────────────────────────────┘\n"));
            break;

        case 'n':
        case 'N':
            Serial.println(F("\n┌── Neighbours ──────────────────────────────────────────┐"));
            printNeighboursToSerial();
            Serial.println(F("└────────────────────────────────────────────────────────┘\n"));
            break;

        case 'r':
        case 'R':
            Serial.println(F("\n⚠ Restarting device..."));
            delay(1000);
            ESP.restart();
            break;

        case 'd':
        case 'D':
            Serial.println(F("\n┌── DEBUG INFO ──────────────────────────────────────────┐"));
            Serial.printf("│ Radio Interrupts:    %u\n", interruptCount);
            Serial.printf("│ Packets Received:    %u\n", packetsReceived);
            Serial.printf("│ Packets Sent:        %u\n", packetsSent);
            Serial.printf("│ Packets Forwarded:   %u\n", packetsForwarded);
            Serial.printf("│ Packets Failed:      %u\n", packetsFailed);
            Serial.printf("│ Radio Initialized:   %s\n", radioInitialized ? "YES" : "NO");
            Serial.printf("│ Packet Flag:         %s\n", packetReceived ? "SET" : "CLEAR");
            // Check radio status
            if (radioInitialized)
            {
                Serial.println(F("│"));
                Serial.print(F("│ Radio status check...  "));
                int state = radio.startReceive();
                if (state == RADIOLIB_ERR_NONE)
                {
                    Serial.println(F("RX ACTIVE"));
                    // Try to read RSSI to verify radio is listening
                    int rssi = radio.getRSSI();
                    Serial.printf("│ Current RSSI:         %d dBm\n", rssi);
                }
                else
                {
                    Serial.printf("RX FAILED (code: %d)\n", state);
                }
            }
            Serial.println(F("└────────────────────────────────────────────────────────┘\n"));
            break;

#ifdef RAK_3112
        case 'm':
        case 'M':
        {
            Serial.println(F("\n📡 Sending MeshCore public-channel message..."));
            uint8_t frame[255];
            uint32_t ts = (uint32_t)time(nullptr);
            if (ts < 1600000000UL) ts = millis() / 1000;   // no NTP yet
            size_t n = meshProto.buildGroupText(frame, sizeof(frame), "hello from RAK3112", ts);
            if (n == 0)
            {
                Serial.println(F("  ✗ Failed to build frame"));
                break;
            }
            Serial.print(F("  frame: "));
            for (size_t i = 0; i < n; ++i) Serial.printf("%02X", frame[i]);
            Serial.println();
            if (sendLoRaPacket(frame, n))
            {
                meshProto.recordOutgoing("(public)", "hello from RAK3112", false);
                Serial.println(F("  ✓ Sent"));
            }
            break;
        }
        case 'k':
        case 'K':
        {
            Serial.println(F("\n🔑 MeshCore identity"));
            Serial.print(F("  public key: "));
            const uint8_t *pk = meshProto.publicKey();
            for (int i = 0; i < 32; ++i) Serial.printf("%02X", pk[i]);
            Serial.println();
            Serial.printf("  self hash : 0x%02X\n", meshProto.selfHash());
            Serial.printf("  chan hash : 0x%02X\n", meshProto.channelHash());
            Serial.printf("  contacts  : %d\n", meshProto.contactCount);
            for (int i = 0; i < MC_MAX_CONTACTS; ++i)
            {
                if (meshProto.contacts[i].used)
                    Serial.printf("    - %s (hash 0x%02X, rssi %d)\n",
                                  meshProto.contacts[i].name,
                                  meshProto.contacts[i].pubKey[0],
                                  meshProto.contacts[i].lastRssi);
            }
            break;
        }
#endif
        case 't':
        case 'T':
            Serial.println(F("\n📡 Sending test packet..."));
            {
                uint8_t testPacket[] = "TEST_GATEWAY_TX";
                if (sendLoRaPacket(testPacket, sizeof(testPacket)))
                {
                    Serial.println(F("✓ Test packet transmitted successfully!"));
                    Serial.println(F("  (Your other radio should receive this if in range)"));
                }
                else
                {
                    Serial.println(F("✗ Test packet transmission failed!"));
                }
            }
            break;
        }
    }
}

void publishStats()
{
    if (mqttHandler)
    {
        mqttHandler->publishStats(packetsReceived, packetsSent, packetsForwarded, packetsFailed);
    }
}

void publishNeighbours()
{
    if (mqttHandler)
    {
        mqttHandler->publishNeighbors(neighbors, neighborCount);
    }
}

void blinkLED()
{
    // TODO: Add LED blinking based on status
    // - Slow blink: Normal operation
    // - Fast blink: WiFi connecting
    // - Solid: MQTT connected
    // You can use the built-in LED or an external LED
}

void sendAdvert()
{
    // Compose a simple advert string: ADVERT <nodeIdHex> <nodeName> <lat> <lon>
    char payload[160];
    snprintf(payload, sizeof(payload), "ADVERT %08X %s %.6f %.6f",
             config.repeater.nodeId,
             config.repeater.nodeName,
             (double)config.location.latitude,
             (double)config.location.longitude);
    sendLoRaPacket((const uint8_t *)payload, strlen(payload));
    // Also publish an advert event on MQTT for visibility if connected
    if (mqttHandler && mqttHandler->isConnected())
    {
        mqttHandler->publishAdvert(
            config.repeater.nodeId,
            config.repeater.nodeName,
            config.location.latitude,
            config.location.longitude);
    }
}

void printTelemetryToSerial()
{
    Serial.printf("Uptime:           %-36lu \n", millis() / 1000);
    Serial.printf("Packets Received: %-36lu \n", packetsReceived);
    Serial.printf("Packets Sent:     %-36lu \n", packetsSent);
    Serial.printf("Packets Forwarded:%-36lu \n", packetsForwarded);
    Serial.printf("Packets Failed:   %-36lu \n", packetsFailed);
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("WiFi RSSI:        %-36d \n", WiFi.RSSI());
        Serial.printf("IP Address:       %-36s \n", WiFi.localIP().toString().c_str());
    }
}

void printNeighboursToSerial()
{
    if (neighborCount == 0)
    {
        Serial.println(F("(none)"));
        return;
    }
    unsigned long now = millis();
    for (size_t i = 0; i < neighborCount; ++i)
    {
        unsigned long age = (now - neighbors[i].lastSeenMs) / 1000UL;
        Serial.printf("ID: 0x%08X  Name: %-16s  RSSI: %4d  SNR: %5.1f  Age: %lus  Lat: %.5f  Lon: %.5f\n",
                      neighbors[i].nodeId,
                      neighbors[i].nodeName,
                      neighbors[i].lastRssi,
                      neighbors[i].lastSnr,
                      age,
                      (double)neighbors[i].latitude,
                      (double)neighbors[i].longitude);
    }
}

// Exit configuration mode helper
void exitConfigMode()
{
    configMode = false;
    Serial.println(F("\n✓ Exited configuration mode"));

    // Restart if configuration changed significantly
    Serial.println(F("⚠ Some changes may require a restart"));
    Serial.println(F("(Hint) Live view resumed. Press 'c' to return to the menu"));
}
