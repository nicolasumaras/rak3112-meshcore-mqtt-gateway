#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "config.h"
#include "settings_manager.h"
#include "mqtt_handler.h"
#include <time.h>

// Provided by main.cpp to print runtime data
void printTelemetryToSerial();
void printNeighboursToSerial();

class ConfigMenu {
public:
    ConfigMenu(GatewayConfig& cfg, SettingsManager& settings) 
        : config(cfg), settingsManager(settings) {}

    void setOnExitCallback(void (*cb)()) { onExitCallback = cb; }
    
    void begin() {
        Serial.println(F("\n╔════════════════════════════════════════════════════════╗"));
        Serial.println(F("║    MeshCore MQTT Gateway - Configuration Menu         ║"));
        Serial.println(F("╚════════════════════════════════════════════════════════╝"));
    }
    
    void showMainMenu() {
        Serial.println(F("\n"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@#+===+%@@@@@@@@@@*====#@@%+===============%@@@%#+============#@@@*===+%@@@@@@%====#@@@@@@@"));
        Serial.println(F("@@@@@@@@@%......+@@@@@@@@%-.....=@@+................%@*:...............=@@*.....#@@@@@%.....=@@@@@@@"));
        Serial.println(F("@@@@@@@@@+......-%@@@@@@#.......#@@:...............-%*.................*@@-....:@@@@@@*.....#@@@@@@@"));
        Serial.println(F("@@@@@@@@%-.......#@@@@@*.......:%@#.....#@@@@@@@@@@@@-....+%%%%%%%%%%%@@@%:....=@@@@@%=....:%@@@@@@@"));
        Serial.println(F("@@@@@@@@#:.......:%@@%=........+@@=.............:#@@%......:::::::::=#@@@*.................+@@@@@@@@"));
        Serial.println(F("@@@@@@@@+.........+@%-.........*@%:.............-%@@@=................+@@=.................*@@@@@@@@"));
        Serial.println(F("@@@@@@@@-....:....:#.....:....:%@#.....-========*@@@@@%+---------:....:%@:....:======:....:%@@@@@@@@"));
        Serial.println(F("@@@@@@@%.....*:.........++....=@@+....:#%%%%%%%%%%@@@%%%%%%%%%%%%:....=@#.....*@@@@@%.....=@@@@@@@@@"));
        Serial.println(F("@@@@@@@*....:%*........*@-....#@@-...............:%%.................:%@=.....%@@@@@*.....#@@@@@@@@@"));
        Serial.println(F("@@@@@@@:....+@@-.....:%@#....:@@%................=@*................=%@%:....=@@@@@@-....:@@@@@@@@@@"));
        Serial.println(F("@@@@@@@#***#@@@%#***#%@@@#**#%@@@#**************#%@%#************#%@@@@@#***#@@@@@@@%***#%@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@%*:............=@@%=...............+%@@%:...............=%@@%:...............-%@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@%:..............*@+..................-@@*.................-%@*................+@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@-.....=========*%%.....-+++++++=.....-@@-....:+++++++.....-%@=....:+++++++++++%@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@%.....=@@@@@@@@@@@*.....%@@@@@@@#.....*@%.....+@@@@@@@:....=@%-....=%%%%%%%%%@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@+.....#@@@@@@@@@@@-....-@@@@@@@@+.....%@*.....%%%%%%%+.....#@#..............*@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@%-....-@@@@@@@@@@@#.....#@@@@@@@%-....=%@=.................=@@+.............:%@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@#:....+@@@@@@@@@@@+....:%@@@@@@@#.....+@%:................*@@@:....=%%%%%%%%@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@*......:::::::::#%=.....::::::::.....:#@*.....+***+......%@@@#......::::::::::*@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@#..............:%@+..................#@@=....-%@@@@#......#@@=................#@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@%=:...........+%@@#-:...........:-#%@@@=...:*@@@@@@#:....=%%=...............-%@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        Serial.println(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"));
        
        Serial.println(F("\n┌────────────────────────────────────────────────────────┐"));
        Serial.println(F("│ MAIN MENU                                              │"));
        Serial.println(F("├────────────────────────────────────────────────────────┤"));
        Serial.println(F("│ 1. WiFi Settings                                       │"));
        Serial.println(F("│ 2. MQTT Settings                                       │"));
        Serial.println(F("│ 3. LoRa Settings                                       │"));
        Serial.println(F("│ 4. Repeater Settings                                   │"));
        Serial.println(F("│ 5. Show Current Configuration                          │"));
        Serial.println(F("│ 6. Save Configuration                                  │"));
        Serial.println(F("│ 7. Reset to Defaults                                   │"));
        Serial.println(F("│ 8. Restart Device                                      │"));
        Serial.println(F("│ 9. Connectivity Check                                  │"));
        Serial.println(F("│ 0. Exit Configuration                                  │"));
        Serial.println(F("├────────────────────────────────────────────────────────┤"));
        Serial.println(F("│ 10. Security                                           │"));
        Serial.println(F("│ 11. Discovery / Advert                                 │"));
        Serial.println(F("│ 12. Location                                           │"));
        Serial.println(F("│ 13. Clock Sync                                         │"));
        Serial.println(F("│ 14. Neighbours                                         │"));
        Serial.println(F("│ 15. Telemetry                                          │"));
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
        Serial.print(F("\nEnter choice: "));
    }
    
    void handleMenu() {
        if (Serial.available()) {
            String input = readLineRaw();
            
            if (input.length() == 0) return;
            
            int choice = input.toInt();
            
            switch (choice) {
                case 1: configureWiFi(); break;
                case 2: configureMQTT(); break;
                case 3: configureLoRa(); break;
                case 4: configureRepeater(); break;
                case 5: showConfiguration(); break;
                case 6: saveConfiguration(); break;
                case 7: resetToDefaults(); break;
                case 8: restartDevice(); break;
                case 9: runConnectivityCheck(); break;
                case 10: configureSecurity(); break;
                case 11: configureDiscovery(); break;
                case 12: configureLocation(); break;
                case 13: configureClock(); break;
                case 14: showNeighbours(); break;
                case 15: showTelemetry(); break;
                case 0:
                    exitConfig();
                    if (onExitCallback) onExitCallback();
                    return;
                default:
                    Serial.println(F("Invalid choice!"));
                    break;
            }
            
            showMainMenu();
        }
    }
    
private:
    GatewayConfig& config;
    SettingsManager& settingsManager;
    WiFiClient netClient;
    void (*onExitCallback)() = nullptr;

    // UI helpers for consistent boxed output (match main.cpp)
    static const int BOX_CONTENT_WIDTH = 54; // content width excluding the single spaces adjacent to the bars

    static void printBoxLine(const String& content) {
        int pad = BOX_CONTENT_WIDTH - (int)content.length();
        if (pad < 0) pad = 0;
        Serial.print("│ ");
        Serial.print(content);
        for (int i = 0; i < pad; ++i) Serial.print(' ');
        Serial.println(" │");
    }

    static void printBoxKeyValue(const char* key, const String& value, int keyWidth = 14) {
        String line = String(key);
        while ((int)line.length() < keyWidth) line += ' ';
        line += value;
        printBoxLine(line);
    }
    
    // Internal helper to read a line with live echo and optional masking; handles CR, LF, CRLF, and backspace
    String readLineInternal(bool maskEcho) {
        String input;
        while (true) {
            while (!Serial.available()) delay(10);
            char c = (char)Serial.read();
            if (c == '\r' || c == '\n') {
                // Swallow optional following \n in CRLF
                if (c == '\r' && Serial.peek() == '\n') {
                    Serial.read();
                }
                break;
            }
            // Handle backspace/delete
            if ((c == 0x08 || c == 0x7F)) {
                if (input.length() > 0) {
                    input.remove(input.length() - 1);
                    // Erase last character from terminal
                    Serial.print("\b \b");
                }
                continue;
            }
            // Printable ASCII range
            if (c >= 32 && c <= 126) {
                input += c;
                Serial.print(maskEcho ? '*' : c);
            }
            // Ignore other control characters
        }
        Serial.println();
        input.trim();
        return input;
    }

    // Reads a line from Serial with live echo (unmasked)
    String readLine(const char* prompt, const String& defaultValue = "") {
        Serial.print(prompt);
        if (defaultValue.length() > 0) {
            Serial.print(F(" ["));
            Serial.print(defaultValue);
            Serial.print(F("]"));
        }
        Serial.print(F(": "));
        String input = readLineInternal(false);
        if (input.length() == 0 && defaultValue.length() > 0) {
            return defaultValue;
        }
        return input;
    }

    // Reads a line with masked echo (e.g., for passwords)
    String readLineMasked(const char* prompt, const String& defaultValue = "") {
        Serial.print(prompt);
        if (defaultValue.length() > 0) {
            Serial.print(F(" ["));
            Serial.print(defaultValue);
            Serial.print(F("]"));
        }
        Serial.print(F(": "));
        String input = readLineInternal(true);
        if (input.length() == 0 && defaultValue.length() > 0) {
            return defaultValue;
        }
        return input;
    }

    // Raw line reader without printing a prompt; live echo (unmasked)
    String readLineRaw() {
        return readLineInternal(false);
    }
    
    bool readBool(const char* prompt, bool defaultValue) {
        String def = defaultValue ? "y" : "n";
        String input = readLine(prompt, def);
        input.toLowerCase();
        return (input == "y" || input == "yes" || input == "1" || input == "true");
    }

    String selectFromList(const char* title, const char* const* options, size_t count, const String& current, bool allowCustom, String* outCustom) {
        Serial.println(title);
        for (size_t i = 0; i < count; ++i) {
            Serial.print(F("  ")); Serial.print(i + 1); Serial.print(F(") ")); Serial.println(options[i]);
        }
        if (allowCustom) {
            Serial.print(F("  ")); Serial.print(count + 1); Serial.println(F(") (Custom)"));
        }
        Serial.print(F("Select [")); Serial.print(current.length() ? current : String("(none)")); Serial.print(F("): "));
        while (!Serial.available()) delay(10);
        String input = readLineRaw();
        if (input.length() == 0) return current; // keep existing
        int sel = input.toInt();
        if (sel >= 1 && sel <= (int)count) {
            return String(options[sel - 1]);
        }
        if (allowCustom && sel == (int)count + 1) {
            String custom = readLine("Enter custom value", current);
            if (outCustom) *outCustom = custom;
            return custom;
        }
        // Fallback: treat input as custom text
        return input;
    }
    
    int readInt(const char* prompt, int defaultValue) {
        String input = readLine(prompt, String(defaultValue));
        return input.toInt();
    }
    
    float readFloat(const char* prompt, float defaultValue) {
        String input = readLine(prompt, String(defaultValue, 2));
        return input.toFloat();
    }

    // Reads a hex byte value from input, supporting formats like "0x12", "12", "0X12"
    uint8_t readHexByte(const char* prompt, uint8_t defaultValue) {
        char defBuf[8];
        snprintf(defBuf, sizeof(defBuf), "0x%02X", (unsigned)defaultValue);
        String input = readLine(prompt, String(defBuf));
        input.trim();
        if (input.length() == 0) {
            return defaultValue;
        }
        // Strip optional 0x/0X
        if (input.startsWith("0x") || input.startsWith("0X")) {
            input.remove(0, 2);
        }
        input.toUpperCase();
        // Keep only hex chars
        String hexOnly;
        for (size_t i = 0; i < input.length(); ++i) {
            char c = input[i];
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) hexOnly += c;
        }
        if (hexOnly.length() == 0) return defaultValue;
        // Parse up to 2 hex digits
        unsigned int value = 0;
        for (size_t i = 0; i < hexOnly.length() && i < 2; ++i) {
            char c = hexOnly[i];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= (unsigned)(c - '0');
            else value |= (unsigned)(10 + c - 'A');
        }
        return (uint8_t)value;
    }
    
    void configureWiFi() {
        Serial.println(F("\n┌── WiFi Configuration ──────────────────────────────────┐"));
        
        config.wifi.enabled = readBool("Enable WiFi (y/n)", config.wifi.enabled);
        
        if (config.wifi.enabled) {
            String ssid = readLine("WiFi SSID", String(config.wifi.ssid));
            strncpy(config.wifi.ssid, ssid.c_str(), sizeof(config.wifi.ssid) - 1);
            config.wifi.ssid[sizeof(config.wifi.ssid) - 1] = '\0';
            
            String pass = readLineMasked("WiFi Password", "********");
            if (pass != "********") {
                strncpy(config.wifi.password, pass.c_str(), sizeof(config.wifi.password) - 1);
                config.wifi.password[sizeof(config.wifi.password) - 1] = '\0';
            }
        }
        
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
        Serial.println(F("✓ WiFi configuration updated"));
    }
    
    void configureMQTT() {
        Serial.println(F("\n┌── MQTT Configuration ──────────────────────────────────┐"));
        
        config.mqtt.enabled = readBool("Enable MQTT (y/n)", config.mqtt.enabled);
        
        if (config.mqtt.enabled) {
            String server = readLine("MQTT Server", String(config.mqtt.server));
            strncpy(config.mqtt.server, server.c_str(), sizeof(config.mqtt.server) - 1);
            config.mqtt.server[sizeof(config.mqtt.server) - 1] = '\0';
            
            config.mqtt.port = readInt("MQTT Port", config.mqtt.port);

            // TLS enable toggle
            config.mqtt.useTLS = readBool("Enable TLS (y/n)", config.mqtt.useTLS);

            // If broker changed from default, clear default credentials so prompts default to empty
            if (strcmp(config.mqtt.server, DEFAULT_MQTT_SERVER) != 0) {
                if (strcmp(config.mqtt.username, DEFAULT_MQTT_USER) == 0) {
                    config.mqtt.username[0] = '\0';
                }
                if (strcmp(config.mqtt.password, DEFAULT_MQTT_PASSWORD) == 0) {
                    config.mqtt.password[0] = '\0';
                }
            }
            
            String user = readLine("MQTT Username", String(config.mqtt.username));
            strncpy(config.mqtt.username, user.c_str(), sizeof(config.mqtt.username) - 1);
            config.mqtt.username[sizeof(config.mqtt.username) - 1] = '\0';
            
            String pass = readLineMasked("MQTT Password", "********");
            if (pass != "********") {
                strncpy(config.mqtt.password, pass.c_str(), sizeof(config.mqtt.password) - 1);
                config.mqtt.password[sizeof(config.mqtt.password) - 1] = '\0';
            }
            
            // Client ID is derived from the repeater node name
            deriveClientIdFromNodeName(config.repeater.nodeName, config.mqtt.clientId, sizeof(config.mqtt.clientId));
            Serial.printf("Client ID (auto from Node Name): %s\n", config.mqtt.clientId);
            
            String base = readLine("Base Prefix (e.g. MESHCORE)", String(config.mqtt.basePrefix));
            strncpy(config.mqtt.basePrefix, base.c_str(), sizeof(config.mqtt.basePrefix) - 1);
            config.mqtt.basePrefix[sizeof(config.mqtt.basePrefix) - 1] = '\0';
            // Country ISO2 selector (CSC database aligned)
            static const char* COUNTRIES[] = {
                "", "AU", "NZ", "US", "CA", "GB"
            };
            String country = selectFromList("Country ISO2 (optional)", COUNTRIES, sizeof(COUNTRIES)/sizeof(COUNTRIES[0]), String(config.mqtt.country), true, nullptr);
            // Normalize to uppercase and strip spaces for robustness
            country.toUpperCase();
            country.replace(" ", "");
            if (country.length() > 0 && country.length() != 2) {
                Serial.println(F("⚠ Country should be ISO2 (2 letters); keeping value but normalizing topic only"));
            }
            strncpy(config.mqtt.country, country.c_str(), sizeof(config.mqtt.country) - 1);
            config.mqtt.country[sizeof(config.mqtt.country) - 1] = '\0';
            // Region/state selector using common ISO-3166-2 code parts
            String region;
            if (country == "AU") {
                static const char* AU_REGIONS[] = { "", "NSW", "VIC", "QLD", "WA", "SA", "TAS", "ACT", "NT" };
                region = selectFromList("Region ISO (optional)", AU_REGIONS, sizeof(AU_REGIONS)/sizeof(AU_REGIONS[0]), String(config.mqtt.region), true, nullptr);
            } else if (country == "US") {
                static const char* US_REGIONS[] = { "", "CA", "NY", "TX", "WA", "FL", "CO" };
                region = selectFromList("Region ISO (optional)", US_REGIONS, sizeof(US_REGIONS)/sizeof(US_REGIONS[0]), String(config.mqtt.region), true, nullptr);
            } else if (country == "NZ") {
                static const char* NZ_REGIONS[] = { "", "AUK", "WGN", "CAN", "OTA", "BOP", "WKO" };
                region = selectFromList("Region ISO (optional)", NZ_REGIONS, sizeof(NZ_REGIONS)/sizeof(NZ_REGIONS[0]), String(config.mqtt.region), true, nullptr);
            } else {
                region = readLine("Region ISO (optional)", String(config.mqtt.region));
            }
            // Normalize region similarly
            region.toUpperCase();
            region.replace(" ", "");
            strncpy(config.mqtt.region, region.c_str(), sizeof(config.mqtt.region) - 1);
            config.mqtt.region[sizeof(config.mqtt.region) - 1] = '\0';
            deriveTopicPrefix(config.mqtt, config.mqtt.topicPrefix, sizeof(config.mqtt.topicPrefix));
            Serial.printf("Effective Topic Prefix: %s\n", config.mqtt.topicPrefix);
            
            config.mqtt.publishRaw = readBool("Publish raw packets (y/n)", config.mqtt.publishRaw);
            config.mqtt.publishDecoded = readBool("Publish decoded messages (y/n)", config.mqtt.publishDecoded);
            config.mqtt.subscribeCommands = readBool("Subscribe to commands (y/n)", config.mqtt.subscribeCommands);

            // Custom CA management
            Serial.println(F("\nTLS CA Options:"));
            // Default to built-in CA when TLS is enabled and no custom CA is provided
            config.mqtt.useCustomCA = readBool("Use custom CA (y/n)", config.mqtt.useCustomCA);
            if (config.mqtt.useTLS && !config.mqtt.useCustomCA) {
                // Optional: allow insecure TLS for testing when broker uses self-signed/unknown chain
                config.mqtt.insecureTLS = readBool("Allow insecure TLS (skip cert validation) (y/n)", config.mqtt.insecureTLS);
            }
            if (config.mqtt.useCustomCA) {
                Serial.println(F("Paste PEM CA certificate below, end with a single line 'ENDCA':"));
                // Read multiple lines until ENDCA
                String pem;
                while (true) {
                while (!Serial.available()) delay(10);
                String line = readLineRaw();
                    if (line == "ENDCA") break;
                    pem += line + "\n";
                }
                strncpy(config.mqtt.caCert, pem.c_str(), sizeof(config.mqtt.caCert) - 1);
                config.mqtt.caCert[sizeof(config.mqtt.caCert) - 1] = '\0';
                Serial.println(F("✓ Custom CA saved (volatile until you Save Configuration)"));
            }
        }
        
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
        Serial.println(F("✓ MQTT configuration updated"));
    }

    void configureSecurity() {
        Serial.println(F("\n┌── Security Configuration ───────────────────────────────┐"));
        String guest = readLineMasked("Guest Password", strlen(config.security.guestPassword) ? "********" : "");
        if (guest.length() > 0 && guest != "********") {
            strncpy(config.security.guestPassword, guest.c_str(), sizeof(config.security.guestPassword) - 1);
            config.security.guestPassword[sizeof(config.security.guestPassword) - 1] = '\0';
        }
        String admin = readLineMasked("Admin Password", strlen(config.security.adminPassword) ? "********" : "");
        if (admin.length() > 0 && admin != "********") {
            strncpy(config.security.adminPassword, admin.c_str(), sizeof(config.security.adminPassword) - 1);
            config.security.adminPassword[sizeof(config.security.adminPassword) - 1] = '\0';
        }
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
        Serial.println(F("✓ Security configuration updated"));
    }

    void configureDiscovery() {
        Serial.println(F("\n┌── Discovery / Advert ───────────────────────────────────┐"));
        config.discovery.advertEnabled = readBool("Enable periodic advert (y/n)", config.discovery.advertEnabled);
        config.discovery.advertIntervalSec = (uint16_t)readInt("Advert interval (seconds)", config.discovery.advertIntervalSec);
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
        Serial.println(F("✓ Discovery configuration updated"));
    }

    void configureLocation() {
        Serial.println(F("\n┌── Location Configuration ───────────────────────────────┐"));
        config.location.latitude = readFloat("Latitude (-90..90)", config.location.latitude);
        config.location.longitude = readFloat("Longitude (-180..180)", config.location.longitude);
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
        Serial.println(F("✓ Location updated"));
    }

    void configureClock() {
        Serial.println(F("\n┌── Clock / NTP ──────────────────────────────────────────┐"));
        String ntp = readLine("NTP Server", String(config.clock.ntpServer));
        strncpy(config.clock.ntpServer, ntp.c_str(), sizeof(config.clock.ntpServer) - 1);
        config.clock.ntpServer[sizeof(config.clock.ntpServer) - 1] = '\0';
        config.clock.timezoneMinutes = readInt("Timezone offset (minutes)", config.clock.timezoneMinutes);
        config.clock.autoSync = readBool("Auto-sync at boot (y/n)", config.clock.autoSync);
        bool doSync = readBool("Sync now (y/n)", true);
        if (doSync) {
            long gmtOffset = (long)config.clock.timezoneMinutes * 60;
            configTime(gmtOffset, 0, config.clock.ntpServer);
            struct tm timeinfo = {};
            const unsigned long start = millis();
            while (!getLocalTime(&timeinfo) && millis() - start < 10000UL) {
                delay(200);
                Serial.print('.');
            }
            if (getLocalTime(&timeinfo)) {
                Serial.println(F("\n✓ Time synced"));
            } else {
                Serial.println(F("\n⚠ Failed to sync time"));
            }
        }
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
        Serial.println(F("✓ Clock settings updated"));
    }

    void showNeighbours() {
        Serial.println(F("\n┌── Neighbours (0-hop) ───────────────────────────────────┐"));
        printNeighboursToSerial();
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
    }

    void showTelemetry() {
        Serial.println(F("\n┌── Telemetry ────────────────────────────────────────────┐"));
        printTelemetryToSerial();
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
    }
    
    void configureLoRa() {
        Serial.println(F("\n┌── LoRa Configuration ──────────────────────────────────┐"));
        
        config.lora.frequency = readFloat("Frequency (MHz)", config.lora.frequency);
        config.lora.bandwidth = readFloat("Bandwidth (kHz)", config.lora.bandwidth);
        config.lora.spreadingFactor = readInt("Spreading Factor (7-12)", config.lora.spreadingFactor);
        config.lora.codingRate = readInt("Coding Rate (5-8)", config.lora.codingRate);
        config.lora.txPower = readInt("TX Power (2-20 dBm)", config.lora.txPower);
        config.lora.syncWord = readHexByte("Sync Word (hex)", config.lora.syncWord);
        config.lora.enableCRC = readBool("Enable CRC (y/n)", config.lora.enableCRC);
        
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
        Serial.println(F("✓ LoRa configuration updated"));
        Serial.println(F("⚠ Restart required for LoRa changes to take effect"));
    }
    
    void configureRepeater() {
        Serial.println(F("\n┌── Repeater Configuration ──────────────────────────────┐"));
        
        String nodeName = readLine("Node Name", String(config.repeater.nodeName));
        strncpy(config.repeater.nodeName, nodeName.c_str(), sizeof(config.repeater.nodeName) - 1);
        config.repeater.nodeName[sizeof(config.repeater.nodeName) - 1] = '\0';
        
        // Keep MQTT Client ID in sync with node name
        deriveClientIdFromNodeName(config.repeater.nodeName, config.mqtt.clientId, sizeof(config.mqtt.clientId));
        Serial.printf("✓ MQTT Client ID updated to: %s\n", config.mqtt.clientId);
        
        config.repeater.maxHops = readInt("Max Hops (1-7)", config.repeater.maxHops);
        config.repeater.autoAck = readBool("Auto ACK (y/n)", config.repeater.autoAck);
        config.repeater.broadcastEnabled = readBool("Broadcast Enabled (y/n)", config.repeater.broadcastEnabled);
        config.repeater.routeTimeout = readInt("Route Timeout (seconds)", config.repeater.routeTimeout);
        
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
        Serial.println(F("✓ Repeater configuration updated"));
    }
    
    void showConfiguration() {
        Serial.println(F("\n╔════════════════════════════════════════════════════════╗"));
        Serial.println(F("║ CURRENT CONFIGURATION                                  ║"));
        Serial.println(F("╠════════════════════════════════════════════════════════╣"));
        
        // WiFi
        Serial.println(F("║ WiFi Settings:                                         ║"));
        Serial.printf("║   Enabled: %-43s ║\n", config.wifi.enabled ? "Yes" : "No");
        Serial.printf("║   SSID: %-46s ║\n", config.wifi.ssid);
        Serial.printf("║   Password: %-42s ║\n", strlen(config.wifi.password) > 0 ? "********" : "(none)");
        
        // MQTT
        Serial.println(F("╠════════════════════════════════════════════════════════╣"));
        Serial.println(F("║ MQTT Settings:                                         ║"));
        Serial.printf("║   Enabled: %-43s ║\n", config.mqtt.enabled ? "Yes" : "No");
        Serial.printf("║   Server: %-46s ║\n", config.mqtt.server);
        Serial.printf("║   Port: %-48d║\n", config.mqtt.port);
        Serial.printf("║   TLS: %-50s║\n", config.mqtt.useTLS ? "Enabled" : "Disabled");
        Serial.printf("║   Username: %-44s ║\n", config.mqtt.username);
        Serial.printf("║   Client ID: %-43s ║\n", config.mqtt.clientId);
        Serial.printf("║   Base Prefix: %-42s ║\n", config.mqtt.basePrefix);
        Serial.printf("║   Country: %-46s ║\n", config.mqtt.country[0] ? config.mqtt.country : "(none)");
        Serial.printf("║   Region: %-47s ║\n", config.mqtt.region[0] ? config.mqtt.region : "(none)");
        Serial.printf("║   Topic Prefix: %-40s ║\n", config.mqtt.topicPrefix);
        Serial.printf("║   Publish Raw: %-41s ║\n", config.mqtt.publishRaw ? "Yes" : "No");
        Serial.printf("║   Publish Decoded: %-37s ║\n", config.mqtt.publishDecoded ? "Yes" : "No");
        Serial.printf("║   TLS Custom CA: %-39s ║\n", config.mqtt.useCustomCA ? "Yes" : "No");
        
        // LoRa
        Serial.println(F("╠════════════════════════════════════════════════════════╣"));
        Serial.println(F("║ LoRa Settings:                                         ║"));
        Serial.printf("║   Frequency: %.2f MHz                                 ║\n", config.lora.frequency);
        Serial.printf("║   Bandwidth: %.1f kHz                                 ║\n", config.lora.bandwidth);
        Serial.printf("║   Spreading Factor: %-35d║\n", config.lora.spreadingFactor);
        Serial.printf("║   Coding Rate: %-40d║\n", config.lora.codingRate);
        Serial.printf("║   TX Power: %-43d║\n", config.lora.txPower);
        Serial.printf("║   Sync Word: 0x%02X                                       ║\n", config.lora.syncWord);
        
        // Repeater
        Serial.println(F("╠════════════════════════════════════════════════════════╣"));
        Serial.println(F("║ Repeater Settings:                                     ║"));
        Serial.printf("║   Node Name: %-43s ║\n", config.repeater.nodeName);
        Serial.printf("║   Node ID: 0x%08X                                  ║\n", config.repeater.nodeId);
        Serial.printf("║   Max Hops: %-44d║\n", config.repeater.maxHops);
        Serial.printf("║   Auto ACK: %-44s║\n", config.repeater.autoAck ? "Yes" : "No");
        Serial.printf("║   Broadcast: %-43s║\n", config.repeater.broadcastEnabled ? "Yes" : "No");
        // Security
        Serial.println(F("╠════════════════════════════════════════════════════════╣"));
        Serial.println(F("║ Security:                                              ║"));
        Serial.printf("║   Guest Password: %-40s ║\n", strlen(config.security.guestPassword) ? "********" : "(none)");
        Serial.printf("║   Admin Password: %-40s ║\n", strlen(config.security.adminPassword) ? "********" : "(none)");
        // Discovery
        Serial.println(F("╠════════════════════════════════════════════════════════╣"));
        Serial.println(F("║ Discovery / Advert:                                    ║"));
        Serial.printf("║   Enabled: %-43s ║\n", config.discovery.advertEnabled ? "Yes" : "No");
        Serial.printf("║   Interval: %-42u ║\n", (unsigned)config.discovery.advertIntervalSec);
        // Location
        Serial.println(F("╠════════════════════════════════════════════════════════╣"));
        Serial.println(F("║ Location:                                              ║"));
        Serial.printf("║   Latitude: %-43.6f ║\n", config.location.latitude);
        Serial.printf("║   Longitude: %-42.6f ║\n", config.location.longitude);
        // Clock
        Serial.println(F("╠════════════════════════════════════════════════════════╣"));
        Serial.println(F("║ Clock / NTP:                                           ║"));
        Serial.printf("║   NTP Server: %-42s ║\n", config.clock.ntpServer);
        Serial.printf("║   Timezone (min): %-38d ║\n", (int)config.clock.timezoneMinutes);
        Serial.printf("║   Auto Sync: %-43s ║\n", config.clock.autoSync ? "Yes" : "No");
        
        Serial.println(F("╚════════════════════════════════════════════════════════╝"));
    }
    
    void saveConfiguration() {
        Serial.print(F("\nSaving configuration... "));
        if (settingsManager.saveConfig(config)) {
            Serial.println(F("✓ Done!"));
            // Offer to run an immediate connectivity + MQTT publish test
            bool doTest = readBool("Run WiFi/MQTT quick test now (y/n)", true);
            if (doTest) {
                if (!config.wifi.enabled || !config.mqtt.enabled) {
                    Serial.println(F("⚠ WiFi or MQTT is disabled; skipping quick test"));
                } else {
                    Serial.println(F("\n┌── Quick Test: WiFi + MQTT ───────────────────────────────┐"));
                    // Use the existing MQTT handler which encapsulates WiFi/TLS/MQTT logic
                    MQTTHandler tester(config);
                    if (tester.begin()) {
                        Serial.println(F("✓ Connected to MQTT broker"));
                        // Publish a retained online status so the user can immediately see traffic
                        tester.publishGatewayStatus(true);
                        char statusTopic[128];
                        snprintf(statusTopic, sizeof(statusTopic), "%s/gateway/%s/status", config.mqtt.topicPrefix, config.mqtt.clientId);
                        Serial.print(F("Published status to: "));
                        Serial.println(statusTopic);
                        Serial.println(F("You can subscribe from your desktop to verify:"));
                        Serial.print(F("  mosquitto_sub -h "));
                        Serial.print(config.mqtt.server);
                        Serial.print(F(" -t \""));
                        Serial.print(config.mqtt.topicPrefix);
                        Serial.println(F("/#\" -v"));
                    } else {
                        Serial.println(F("✗ Quick test failed to connect to MQTT"));
                        Serial.println(F("Hint: Use 'Connectivity Check' from the main menu for diagnostics."));
                    }
                    Serial.println(F("└────────────────────────────────────────────────────────┘"));
                }
            }
        } else {
            Serial.println(F("✗ Failed!"));
        }
    }
    
    void resetToDefaults() {
        Serial.print(F("\n⚠ Reset to factory defaults? (y/n): "));
        while (!Serial.available()) delay(10);
        String input = readLineRaw();
        input.toLowerCase();
        
        if (input == "y" || input == "yes") {
            config = getDefaultConfig();
            settingsManager.clearConfig();
            Serial.println(F("✓ Configuration reset to defaults"));
            Serial.println(F("⚠ Don't forget to save!"));
        } else {
            Serial.println(F("Cancelled"));
        }
    }
    
    void restartDevice() {
        Serial.print(F("\n⚠ Restart device now? (y/n): "));
        while (!Serial.available()) delay(10);
        String input = readLineRaw();
        input.toLowerCase();
        
        if (input == "y" || input == "yes") {
            Serial.println(F("\nRestarting..."));
            delay(1000);
            ESP.restart();
        } else {
            Serial.println(F("Cancelled"));
        }
    }
    
    void runConnectivityCheck() {
        Serial.println(F("\n┌── Connectivity Check ──────────────────────────────────┐"));
        // 1) WiFi status
        wl_status_t st = WiFi.status();
        printBoxKeyValue("WiFi:", st == WL_CONNECTED ? String("Connected") : String("Not Connected"), 8);
        if (st == WL_CONNECTED) {
            printBoxKeyValue("IP:", WiFi.localIP().toString(), 8);
            printBoxKeyValue("RSSI:", String(WiFi.RSSI()) + " dBm", 8);
        }
        
        // 2) DNS resolution test for broker
        bool dnsOk = false;
        IPAddress brokerIp;
        String dnsLabel = strlen(config.mqtt.server) ? String(config.mqtt.server) : String("(not set)");
        if (strlen(config.mqtt.server) > 0) {
            dnsOk = WiFi.hostByName(config.mqtt.server, brokerIp);
        }
        if (dnsOk) {
            printBoxLine(String("DNS (") + dnsLabel + "): OK -> " + brokerIp.toString());
        } else {
            printBoxLine(String("DNS (") + dnsLabel + "): FAIL");
        }
        
        // 3) TCP connect to MQTT port
        bool tcpOk = false;
        if (dnsOk && brokerIp) {
            tcpOk = netClient.connect(brokerIp, config.mqtt.port);
            if (tcpOk) netClient.stop();
            printBoxLine(String("TCP ") + config.mqtt.server + ":" + String(config.mqtt.port) + (tcpOk ? " OK" : " FAIL"));
        } else {
            printBoxLine(String("TCP check skipped (DNS failed)"));
        }
        
        // 4) External reachability test (example.com:80)
        bool extOk = false;
        IPAddress extIp;
        if (WiFi.hostByName("example.com", extIp)) {
            extOk = netClient.connect(extIp, 80);
            if (extOk) netClient.stop();
            printBoxLine(String("External example.com:80 ") + (extOk ? "OK" : "FAIL"));
        } else {
            printBoxLine(String("DNS example.com failed"));
        }
        
        Serial.println(F("└────────────────────────────────────────────────────────┘"));
    }
    
    void exitConfig() {
        Serial.println(F("\nExiting configuration menu..."));
        Serial.println(F("Press 'c' to return to configuration\n"));
    }
};

#endif // SERIAL_CONFIG_H

