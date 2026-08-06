#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

class SettingsManager {
public:
    SettingsManager() : prefs() {}
    
    bool begin() {
        return prefs.begin(CONFIG_NAMESPACE, false);
    }
    
    void end() {
        prefs.end();
    }
    
    bool saveConfig(const GatewayConfig& config) {
        prefs.begin(CONFIG_NAMESPACE, false);
        
        // Save magic number to indicate valid config
        prefs.putUInt("magic", config.magic);
        
        // WiFi settings
        prefs.putString("wifi_ssid", config.wifi.ssid);
        prefs.putString("wifi_pass", config.wifi.password);
        prefs.putBool("wifi_en", config.wifi.enabled);
        
        // MQTT settings
        prefs.putString("mqtt_srv", config.mqtt.server);
        prefs.putUShort("mqtt_port", config.mqtt.port);
        prefs.putString("mqtt_user", config.mqtt.username);
        prefs.putString("mqtt_pass", config.mqtt.password);
        prefs.putString("mqtt_id", config.mqtt.clientId);
        // Persist hierarchical topic fields
        prefs.putString("mqtt_base", config.mqtt.basePrefix);
        prefs.putString("mqtt_country", config.mqtt.country);
        prefs.putString("mqtt_region", config.mqtt.region);
        // Also persist effective prefix for compatibility
        prefs.putString("mqtt_pfx", config.mqtt.topicPrefix);
        prefs.putBool("mqtt_tls", config.mqtt.useTLS);
        prefs.putBool("mqtt_tls_insec", config.mqtt.insecureTLS);
        prefs.putBool("mqtt_en", config.mqtt.enabled);
        prefs.putBool("mqtt_raw", config.mqtt.publishRaw);
        prefs.putBool("mqtt_dec", config.mqtt.publishDecoded);
        prefs.putBool("mqtt_cmd", config.mqtt.subscribeCommands);
        prefs.putBool("mqtt_bridge", config.mqtt.bridgeAll);
        prefs.putBool("mqtt_custca", config.mqtt.useCustomCA);
        prefs.putString("mqtt_cacert", config.mqtt.caCert);
        
        // LoRa settings
        prefs.putFloat("lora_freq", config.lora.frequency);
        prefs.putFloat("lora_bw", config.lora.bandwidth);
        prefs.putUChar("lora_sf", config.lora.spreadingFactor);
        prefs.putUChar("lora_cr", config.lora.codingRate);
        prefs.putUChar("lora_pwr", config.lora.txPower);
        prefs.putUChar("lora_sw", config.lora.syncWord);
        prefs.putBool("lora_crc", config.lora.enableCRC);
        
        // Repeater settings
        prefs.putString("rep_name", config.repeater.nodeName);
        prefs.putUInt("rep_id", config.repeater.nodeId);
        prefs.putUChar("rep_hops", config.repeater.maxHops);
        prefs.putBool("rep_ack", config.repeater.autoAck);
        prefs.putBool("rep_bc", config.repeater.broadcastEnabled);
        prefs.putUShort("rep_tout", config.repeater.routeTimeout);

        // Security
        prefs.putString("sec_guest", config.security.guestPassword);
        prefs.putString("sec_admin", config.security.adminPassword);

        // Access control (denylist)
        prefs.putBool("ac_deny_en", config.access.denyEnabled);
        prefs.putUChar("ac_deny_cnt", config.access.denyCount);
        // Store up to 16 denylist entries
        for (uint8_t i = 0; i < config.access.denyCount && i < 16; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "ac_dn_%02u", i);
            prefs.putUInt(key, config.access.denylist[i]);
        }

        // Discovery
        prefs.putBool("disc_en", config.discovery.advertEnabled);
        prefs.putUShort("disc_int", config.discovery.advertIntervalSec);

        // Location
        prefs.putFloat("loc_lat", config.location.latitude);
        prefs.putFloat("loc_lon", config.location.longitude);

        // Clock
        prefs.putString("clk_ntp", config.clock.ntpServer);
        prefs.putShort("clk_tz", config.clock.timezoneMinutes);
        prefs.putBool("clk_auto", config.clock.autoSync);
        
        prefs.end();
        return true;
    }
    
    bool loadConfig(GatewayConfig& config) {
        prefs.begin(CONFIG_NAMESPACE, true);
        
        // Check if config exists
        uint32_t magic = prefs.getUInt("magic", 0);
        if (magic != CONFIG_MAGIC) {
            prefs.end();
            return false;
        }
        
        config.magic = magic;
        
        // WiFi settings
        strncpy(config.wifi.ssid, prefs.getString("wifi_ssid", "").c_str(), sizeof(config.wifi.ssid) - 1);
        config.wifi.ssid[sizeof(config.wifi.ssid) - 1] = '\0';
        strncpy(config.wifi.password, prefs.getString("wifi_pass", "").c_str(), sizeof(config.wifi.password) - 1);
        config.wifi.password[sizeof(config.wifi.password) - 1] = '\0';
        config.wifi.enabled = prefs.getBool("wifi_en", false);
        
        // MQTT settings
        strncpy(config.mqtt.server, prefs.getString("mqtt_srv", DEFAULT_MQTT_SERVER).c_str(), sizeof(config.mqtt.server) - 1);
        config.mqtt.server[sizeof(config.mqtt.server) - 1] = '\0';
        config.mqtt.port = prefs.getUShort("mqtt_port", DEFAULT_MQTT_PORT);
        strncpy(config.mqtt.username, prefs.getString("mqtt_user", "").c_str(), sizeof(config.mqtt.username) - 1);
        config.mqtt.username[sizeof(config.mqtt.username) - 1] = '\0';
        strncpy(config.mqtt.password, prefs.getString("mqtt_pass", "").c_str(), sizeof(config.mqtt.password) - 1);
        config.mqtt.password[sizeof(config.mqtt.password) - 1] = '\0';
        strncpy(config.mqtt.clientId, prefs.getString("mqtt_id", DEFAULT_MQTT_CLIENT_ID).c_str(), sizeof(config.mqtt.clientId) - 1);
        config.mqtt.clientId[sizeof(config.mqtt.clientId) - 1] = '\0';
        // Load hierarchical topic fields; migrate legacy mqtt_pfx if needed
        String loadedBase = prefs.getString("mqtt_base", "");
        String legacyPrefix = prefs.getString("mqtt_pfx", "");
        if (loadedBase.length() == 0 && legacyPrefix.length() > 0) {
            // Parse legacy prefix like: base[/country[/region]]
            String base = "";
            String country = "";
            String region = "";
            int p1 = legacyPrefix.indexOf('/');
            if (p1 < 0) {
                base = legacyPrefix;
            } else {
                base = legacyPrefix.substring(0, p1);
                int p2 = legacyPrefix.indexOf('/', p1 + 1);
                if (p2 < 0) {
                    country = legacyPrefix.substring(p1 + 1);
                } else {
                    country = legacyPrefix.substring(p1 + 1, p2);
                    region = legacyPrefix.substring(p2 + 1);
                }
            }
            strncpy(config.mqtt.basePrefix, base.c_str(), sizeof(config.mqtt.basePrefix) - 1);
            config.mqtt.basePrefix[sizeof(config.mqtt.basePrefix) - 1] = '\0';
            strncpy(config.mqtt.country, country.c_str(), sizeof(config.mqtt.country) - 1);
            config.mqtt.country[sizeof(config.mqtt.country) - 1] = '\0';
            strncpy(config.mqtt.region, region.c_str(), sizeof(config.mqtt.region) - 1);
            config.mqtt.region[sizeof(config.mqtt.region) - 1] = '\0';
            // Persist migrated fields
            prefs.end();
            prefs.begin(CONFIG_NAMESPACE, false);
            prefs.putString("mqtt_base", config.mqtt.basePrefix);
            prefs.putString("mqtt_country", config.mqtt.country);
            prefs.putString("mqtt_region", config.mqtt.region);
            prefs.begin(CONFIG_NAMESPACE, true);
        } else {
            strncpy(config.mqtt.basePrefix, (loadedBase.length() ? loadedBase : String(DEFAULT_MQTT_TOPIC_PREFIX)).c_str(), sizeof(config.mqtt.basePrefix) - 1);
            config.mqtt.basePrefix[sizeof(config.mqtt.basePrefix) - 1] = '\0';
            strncpy(config.mqtt.country, prefs.getString("mqtt_country", "").c_str(), sizeof(config.mqtt.country) - 1);
            config.mqtt.country[sizeof(config.mqtt.country) - 1] = '\0';
            strncpy(config.mqtt.region, prefs.getString("mqtt_region", "").c_str(), sizeof(config.mqtt.region) - 1);
            config.mqtt.region[sizeof(config.mqtt.region) - 1] = '\0';
        }
        // Build effective prefix
        deriveTopicPrefix(config.mqtt, config.mqtt.topicPrefix, sizeof(config.mqtt.topicPrefix));
        config.mqtt.useTLS = prefs.getBool("mqtt_tls", false);
        config.mqtt.insecureTLS = prefs.getBool("mqtt_tls_insec", false);
        config.mqtt.enabled = prefs.getBool("mqtt_en", false);
        config.mqtt.publishRaw = prefs.getBool("mqtt_raw", true);
        config.mqtt.publishDecoded = prefs.getBool("mqtt_dec", true);
        config.mqtt.subscribeCommands = prefs.getBool("mqtt_cmd", true);
        config.mqtt.bridgeAll = prefs.getBool("mqtt_bridge", true);
        config.mqtt.useCustomCA = prefs.getBool("mqtt_custca", false);
        strncpy(config.mqtt.caCert, prefs.getString("mqtt_cacert", "").c_str(), sizeof(config.mqtt.caCert) - 1);
        config.mqtt.caCert[sizeof(config.mqtt.caCert) - 1] = '\0';
        
        // LoRa settings
        config.lora.frequency = prefs.getFloat("lora_freq", DEFAULT_LORA_FREQ);
        config.lora.bandwidth = prefs.getFloat("lora_bw", DEFAULT_LORA_BW);
        config.lora.spreadingFactor = prefs.getUChar("lora_sf", DEFAULT_LORA_SF);
        config.lora.codingRate = prefs.getUChar("lora_cr", DEFAULT_LORA_CR);
        config.lora.txPower = prefs.getUChar("lora_pwr", DEFAULT_LORA_TX_POWER);
        config.lora.syncWord = prefs.getUChar("lora_sw", 0x12);
        config.lora.enableCRC = prefs.getBool("lora_crc", true);
        
        // Repeater settings
        strncpy(config.repeater.nodeName, prefs.getString("rep_name", "MQTT-Gateway").c_str(), sizeof(config.repeater.nodeName) - 1);
        config.repeater.nodeName[sizeof(config.repeater.nodeName) - 1] = '\0';
        config.repeater.nodeId = prefs.getUInt("rep_id", 0);
        config.repeater.maxHops = prefs.getUChar("rep_hops", 3);
        config.repeater.autoAck = prefs.getBool("rep_ack", true);
        config.repeater.broadcastEnabled = prefs.getBool("rep_bc", true);
        config.repeater.routeTimeout = prefs.getUShort("rep_tout", 300);

        // Security
        strncpy(config.security.guestPassword, prefs.getString("sec_guest", "").c_str(), sizeof(config.security.guestPassword) - 1);
        config.security.guestPassword[sizeof(config.security.guestPassword) - 1] = '\0';
        strncpy(config.security.adminPassword, prefs.getString("sec_admin", "").c_str(), sizeof(config.security.adminPassword) - 1);
        config.security.adminPassword[sizeof(config.security.adminPassword) - 1] = '\0';

        // Access control (denylist)
        config.access.denyEnabled = prefs.getBool("ac_deny_en", false);
        config.access.denyCount = prefs.getUChar("ac_deny_cnt", 0);
        if (config.access.denyCount > 16) config.access.denyCount = 16;
        for (uint8_t i = 0; i < config.access.denyCount; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "ac_dn_%02u", i);
            config.access.denylist[i] = prefs.getUInt(key, 0u);
        }
        for (uint8_t i = config.access.denyCount; i < 16; ++i) {
            config.access.denylist[i] = 0;
        }

        // Discovery
        config.discovery.advertEnabled = prefs.getBool("disc_en", false);
        config.discovery.advertIntervalSec = prefs.getUShort("disc_int", 300);

        // Location
        config.location.latitude = prefs.getFloat("loc_lat", 0.0f);
        config.location.longitude = prefs.getFloat("loc_lon", 0.0f);

        // Clock
        strncpy(config.clock.ntpServer, prefs.getString("clk_ntp", "pool.ntp.org").c_str(), sizeof(config.clock.ntpServer) - 1);
        config.clock.ntpServer[sizeof(config.clock.ntpServer) - 1] = '\0';
        config.clock.timezoneMinutes = prefs.getShort("clk_tz", 0);
        config.clock.autoSync = prefs.getBool("clk_auto", true);
        
        // Ensure MQTT Client ID follows the repeater node name
        deriveClientIdFromNodeName(config.repeater.nodeName, config.mqtt.clientId, sizeof(config.mqtt.clientId));
        
        prefs.end();
        return true;
    }
    
    void clearConfig() {
        prefs.begin(CONFIG_NAMESPACE, false);
        prefs.clear();
        prefs.end();
    }
    
private:
    Preferences prefs;
};

#endif // SETTINGS_MANAGER_H

