#ifndef MESHCORE_PROTO_H
#define MESHCORE_PROTO_H

// ---------------------------------------------------------------------------
// Minimal MeshCore protocol layer.
//
// Enough of meshcore-dev/MeshCore to interoperate with real nodes: build and
// parse group text, adverts and direct messages. Deliberately not a port of
// MeshCore - no routing tables, no packet pool, no transport codes.
//
// Wire format (src/Packet.cpp):
//   [header:1][transport_codes:4 if TRANSPORT_*][path_len:1][path:N][payload]
//
//   header    bits 0-1 route type, 2-5 payload type, 6-7 version
//   path_len  ((hash_size - 1) << 6) | hash_count
//
// Payloads:
//   GRP_TXT  [chan_hash:1][MAC:2][enc: [ts:4][txt_type:1]["name: text"]]
//   TXT_MSG  [dest_hash:1][src_hash:1][MAC:2][enc: [ts:4][txt_type:1][text]]
//   ADVERT   [pub_key:32][timestamp:4][signature:64][app_data:N]
//
// Crypto (src/Utils.cpp):
//   encryptThenMAC = AES-128-ECB(secret[0:16], zero-padded) then
//                    HMAC-SHA256(secret[0:32], ciphertext)[0:2] prepended
//
// A node's "hash" is simply the first byte of its public key - Identity.h does
// memcpy(dest, pub_key, PATH_HASH_SIZE), it is not a digest.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <Preferences.h>
#include <AES.h>
#include <SHA256.h>
#include <ed_25519.h>

#include "config.h"

#define MC_PUB_KEY_SIZE     32
#define MC_PRV_KEY_SIZE     64
#define MC_SIGNATURE_SIZE   64
#define MC_SEED_SIZE        32
#define MC_CIPHER_KEY_SIZE  16
#define MC_CIPHER_BLOCK     16
#define MC_CIPHER_MAC_SIZE   2
#define MC_PATH_HASH_SIZE    1
#define MC_MAX_ADVERT_DATA  32

#define MC_PT_TXT_MSG   0x02
#define MC_PT_ACK       0x03
#define MC_PT_ADVERT    0x04
#define MC_PT_PATH      0x08
#define MC_PT_GRP_TXT   0x05

#define MC_RT_FLOOD     0x01
#define MC_RT_DIRECT    0x02

#define MC_ADV_TYPE_CHAT    1
#define MC_ADV_LATLON_MASK  0x10
#define MC_ADV_NAME_MASK    0x80

#define MC_TXT_TYPE_PLAIN   0

#define MC_MAX_CONTACTS     16

// Throttle for non-durable contact refreshes (RSSI / replay timestamp only).
#define CONTACT_SAVE_INTERVAL_MS  (30UL * 60UL * 1000UL)
#define MC_MAX_MESSAGES     24

struct MCContact
{
    uint8_t pubKey[MC_PUB_KEY_SIZE];
    char name[32];
    uint32_t lastAdvert;   // timestamp from their advert (replay guard)
    int16_t lastRssi;
    bool used;
};

struct MCMessage
{
    char from[32];
    char text[160];
    uint32_t timestamp;    // device millis/1000 when received
    int16_t rssi;
    bool isDirect;
    bool outgoing;
};

class MeshCoreProto
{
public:
    typedef bool (*SendFn)(const uint8_t *data, size_t len);
    // Called for every decoded inbound message so callers can fan out (webhook,
    // MQTT, ...) without this class knowing anything about them.
    typedef void (*RecvFn)(const char *from, const char *text, int rssi, bool isDirect);

    MeshCoreProto() : contactCount(0), sender(NULL), receiver(NULL), msgHead(0), msgCount(0) {}

    // Needed so that received direct messages can be acknowledged. Without an
    // ACK the sending node reports the transmit as failed, even though the
    // message arrived and decrypted correctly.
    void setSender(SendFn fn) { sender = fn; }
    void setReceiver(RecvFn fn) { receiver = fn; }

    // ---- identity -------------------------------------------------------

    // Loads the keypair from NVS, generating and persisting one on first run.
    void begin(const char *nodeName)
    {
        strncpy(name, nodeName, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        Preferences p;
        p.begin("mc_ident", false);
        size_t got = p.getBytes("prv", prvKey, sizeof(prvKey));
        if (got == sizeof(prvKey))
        {
            p.getBytes("pub", pubKey, sizeof(pubKey));
        }
        else
        {
            uint8_t seed[MC_SEED_SIZE];
            for (size_t i = 0; i < sizeof(seed); ++i)
            {
                // esp_random() is a hardware TRNG once RF is up.
                seed[i] = (uint8_t)(esp_random() & 0xFF);
            }
            ed25519_create_keypair(pubKey, prvKey, seed);
            p.putBytes("prv", prvKey, sizeof(prvKey));
            p.putBytes("pub", pubKey, sizeof(pubKey));
            Serial.println(F("✓ Generated new MeshCore identity"));
        }
        advertSeq = p.getUInt("advseq", 0);
        p.end();

        loadContacts();

        Serial.print(F("  MeshCore public key: "));
        for (int i = 0; i < 8; ++i) Serial.printf("%02X", pubKey[i]);
        Serial.println(F("..."));
    }

    const uint8_t *publicKey() const { return pubKey; }
    uint8_t selfHash() const { return pubKey[0]; }

    void setChannelPsk(const uint8_t *psk, size_t len)
    {
        memset(chanSecret, 0, sizeof(chanSecret));
        memcpy(chanSecret, psk, len > 32 ? 32 : len);
        // addChannel() hashes only the PSK-length prefix.
        SHA256 sha;
        uint8_t out[32];
        sha.reset();
        sha.update(psk, len);
        sha.finalize(out, sizeof(out));
        chanHash = out[0];
    }

    uint8_t channelHash() const { return chanHash; }

    // ---- frame builders --------------------------------------------------

    // Public/group text. Returns frame length, or 0 on failure.
    size_t buildGroupText(uint8_t *out, size_t outSize, const char *text, uint32_t ts)
    {
        uint8_t plain[192];
        int n = 0;
        memcpy(&plain[n], &ts, 4); n += 4;
        plain[n++] = MC_TXT_TYPE_PLAIN;
        n += snprintf((char *)&plain[n], sizeof(plain) - n, "%s: %s", name, text);
        if (n <= 5) return 0;

        uint8_t payload[224];
        int pl = 0;
        payload[pl++] = chanHash;
        int enc = encryptThenMAC(chanSecret, &payload[pl], plain, n);
        if (enc <= 0) return 0;
        pl += enc;

        return assemble(out, outSize, MC_PT_GRP_TXT, MC_RT_FLOOD, payload, pl);
    }

    // Direct message to a contact. Returns frame length, or 0 on failure.
    size_t buildDirectText(uint8_t *out, size_t outSize, const MCContact &to,
                           const char *text, uint32_t ts)
    {
        uint8_t secret[MC_PUB_KEY_SIZE];
        ed25519_key_exchange(secret, to.pubKey, prvKey);

        uint8_t plain[192];
        int n = 0;
        memcpy(&plain[n], &ts, 4); n += 4;
        plain[n++] = MC_TXT_TYPE_PLAIN;
        n += snprintf((char *)&plain[n], sizeof(plain) - n, "%s", text);
        if (n <= 5) return 0;

        uint8_t payload[224];
        int pl = 0;
        payload[pl++] = to.pubKey[0];   // dest hash
        payload[pl++] = pubKey[0];      // src hash
        int enc = encryptThenMAC(secret, &payload[pl], plain, n);
        if (enc <= 0) return 0;
        pl += enc;

        return assemble(out, outSize, MC_PT_TXT_MSG, MC_RT_FLOOD, payload, pl);
    }

    // Signed self-advert so other nodes can add us as a contact.
    size_t buildAdvert(uint8_t *out, size_t outSize, uint32_t ts,
                       bool includeLoc, double lat, double lon)
    {
        uint8_t appData[MC_MAX_ADVERT_DATA];
        int a = 0;
        appData[a++] = MC_ADV_TYPE_CHAT;
        if (includeLoc)
        {
            appData[0] |= MC_ADV_LATLON_MASK;
            int32_t ilat = (int32_t)(lat * 1E6);
            int32_t ilon = (int32_t)(lon * 1E6);
            memcpy(&appData[a], &ilat, 4); a += 4;
            memcpy(&appData[a], &ilon, 4); a += 4;
        }
        if (name[0])
        {
            appData[0] |= MC_ADV_NAME_MASK;
            for (const char *sp = name; *sp && a < MC_MAX_ADVERT_DATA; ++sp)
            {
                appData[a++] = (uint8_t)*sp;   // NOT null terminated
            }
        }

        // Signature covers pub_key || timestamp || app_data.
        uint8_t message[MC_PUB_KEY_SIZE + 4 + MC_MAX_ADVERT_DATA];
        int m = 0;
        memcpy(&message[m], pubKey, MC_PUB_KEY_SIZE); m += MC_PUB_KEY_SIZE;
        memcpy(&message[m], &ts, 4); m += 4;
        memcpy(&message[m], appData, a); m += a;

        uint8_t sig[MC_SIGNATURE_SIZE];
        ed25519_sign(sig, message, m, pubKey, prvKey);

        uint8_t payload[MC_PUB_KEY_SIZE + 4 + MC_SIGNATURE_SIZE + MC_MAX_ADVERT_DATA];
        int pl = 0;
        memcpy(&payload[pl], pubKey, MC_PUB_KEY_SIZE); pl += MC_PUB_KEY_SIZE;
        memcpy(&payload[pl], &ts, 4); pl += 4;
        memcpy(&payload[pl], sig, MC_SIGNATURE_SIZE); pl += MC_SIGNATURE_SIZE;
        memcpy(&payload[pl], appData, a); pl += a;

        return assemble(out, outSize, MC_PT_ADVERT, MC_RT_FLOOD, payload, pl);
    }

    // ---- inbound ---------------------------------------------------------

    // Parse a received frame; records contacts and decrypted messages.
    // Returns true if it was understood.
    bool handleFrame(const uint8_t *data, size_t len, int rssi)
    {
        if (len < 3) return false;
        uint8_t header = data[0];
        uint8_t ptype = (header >> 2) & 0x0F;

        size_t off = 1;
        uint8_t route = header & 0x03;
        if (route == 0x00 || route == 0x03) off += 4;   // transport codes
        if (len < off + 1) return false;
        uint8_t pathLen = data[off];
        size_t pathBytes = (size_t)(pathLen & 63) * ((pathLen >> 6) + 1);
        size_t ps = off + 1 + pathBytes;
        if (ps >= len) return false;

        const uint8_t *payload = data + ps;
        size_t plen = len - ps;

        bool isFlood = (route == 0x00 || route == 0x01);
        const uint8_t *path = data + off + 1;

        if (ptype == MC_PT_ADVERT) return handleAdvert(payload, plen, rssi);
        if (ptype == MC_PT_GRP_TXT) return handleGroupText(payload, plen, rssi);
        if (ptype == MC_PT_TXT_MSG)
            return handleDirectText(payload, plen, rssi, isFlood, pathLen, path);
        return false;
    }

    // ---- accessors for the web UI ---------------------------------------

    int contactCount;
    MCContact contacts[MC_MAX_CONTACTS];
    SendFn sender;
    RecvFn receiver;

    int messageCount() const { return msgCount; }
    const MCMessage &messageAt(int i) const
    {
        // newest first
        int idx = (msgHead - 1 - i + MC_MAX_MESSAGES * 2) % MC_MAX_MESSAGES;
        return messages[idx];
    }

    void recordOutgoing(const char *to, const char *text, bool direct)
    {
        MCMessage m = {};
        strncpy(m.from, to, sizeof(m.from) - 1);
        strncpy(m.text, text, sizeof(m.text) - 1);
        m.timestamp = millis() / 1000;
        m.isDirect = direct;
        m.outgoing = true;
        push(m);
    }

    // Contacts must survive reboots: a direct message can only be decrypted if
    // we already hold the sender's public key, and MeshCore nodes only
    // re-advertise periodically. Without this, every restart silently drops
    // inbound direct messages until the peer happens to advertise again.
    void saveContacts()
    {
        Preferences p;
        p.begin("mc_ident", false);
        p.putBytes("contacts", contacts, sizeof(contacts));
        p.end();
    }

    void loadContacts()
    {
        Preferences p;
        p.begin("mc_ident", true);
        size_t got = p.getBytes("contacts", contacts, sizeof(contacts));
        p.end();
        if (got != sizeof(contacts))
        {
            memset(contacts, 0, sizeof(contacts));
            return;
        }
        int n = 0;
        for (int i = 0; i < MC_MAX_CONTACTS; ++i)
        {
            if (contacts[i].used) n = i + 1;
        }
        contactCount = n;
        if (n) Serial.printf("  Restored %d MeshCore contact(s)\n", n);
    }

    void forgetContacts()
    {
        memset(contacts, 0, sizeof(contacts));
        contactCount = 0;
        saveContacts();
    }

    uint32_t nextAdvertSeq()
    {
        Preferences p;
        p.begin("mc_ident", false);
        advertSeq++;
        p.putUInt("advseq", advertSeq);
        p.end();
        return advertSeq;
    }

private:
    uint8_t pubKey[MC_PUB_KEY_SIZE];
    uint8_t prvKey[MC_PRV_KEY_SIZE];
    uint8_t chanSecret[32];
    uint8_t chanHash = 0;
    char name[32];
    uint32_t advertSeq = 0;
    unsigned long lastContactSave = 0;

    MCMessage messages[MC_MAX_MESSAGES];
    int msgHead;
    int msgCount;

    void push(const MCMessage &m)
    {
        messages[msgHead] = m;
        msgHead = (msgHead + 1) % MC_MAX_MESSAGES;
        if (msgCount < MC_MAX_MESSAGES) msgCount++;
    }

    size_t assemble(uint8_t *out, size_t outSize, uint8_t ptype, uint8_t route,
                    const uint8_t *payload, size_t plen)
    {
        if (2 + plen > outSize || 2 + plen > 255) return 0;
        out[0] = (uint8_t)((ptype << 2) | route);
        out[1] = 0x00;   // path_len: hash size 1, count 0
        memcpy(&out[2], payload, plen);
        return 2 + plen;
    }

    // AES-128-ECB over zero-padded input, MAC prepended. Mirrors Utils.cpp.
    int encryptThenMAC(const uint8_t *secret, uint8_t *dest, const uint8_t *src, int srcLen)
    {
        uint8_t padded[208];
        int blocks = (srcLen + 15) / 16;
        if (blocks * 16 > (int)sizeof(padded)) return 0;
        memset(padded, 0, blocks * 16);
        memcpy(padded, src, srcLen);

        AES128 aes;
        aes.setKey(secret, MC_CIPHER_KEY_SIZE);
        for (int i = 0; i < blocks; ++i)
        {
            aes.encryptBlock(dest + MC_CIPHER_MAC_SIZE + i * 16, padded + i * 16);
        }
        int encLen = blocks * 16;

        SHA256 sha;
        sha.resetHMAC(secret, MC_PUB_KEY_SIZE);
        sha.update(dest + MC_CIPHER_MAC_SIZE, encLen);
        sha.finalizeHMAC(secret, MC_PUB_KEY_SIZE, dest, MC_CIPHER_MAC_SIZE);

        return MC_CIPHER_MAC_SIZE + encLen;
    }

    // Verify MAC then decrypt in place. Returns plaintext length or 0.
    int macThenDecrypt(const uint8_t *secret, uint8_t *dest, const uint8_t *src, int srcLen)
    {
        if (srcLen <= MC_CIPHER_MAC_SIZE) return 0;
        int encLen = srcLen - MC_CIPHER_MAC_SIZE;
        if (encLen % 16) return 0;

        uint8_t mac[MC_CIPHER_MAC_SIZE];
        SHA256 sha;
        sha.resetHMAC(secret, MC_PUB_KEY_SIZE);
        sha.update(src + MC_CIPHER_MAC_SIZE, encLen);
        sha.finalizeHMAC(secret, MC_PUB_KEY_SIZE, mac, sizeof(mac));
        if (memcmp(mac, src, MC_CIPHER_MAC_SIZE) != 0) return 0;   // not for us

        AES128 aes;
        aes.setKey(secret, MC_CIPHER_KEY_SIZE);
        for (int i = 0; i < encLen / 16; ++i)
        {
            aes.decryptBlock(dest + i * 16, src + MC_CIPHER_MAC_SIZE + i * 16);
        }
        return encLen;
    }

    bool handleAdvert(const uint8_t *p, size_t len, int rssi)
    {
        size_t fixed = MC_PUB_KEY_SIZE + 4 + MC_SIGNATURE_SIZE;
        if (len < fixed + 1) return false;

        const uint8_t *pk = p;
        uint32_t ts;
        memcpy(&ts, p + MC_PUB_KEY_SIZE, 4);
        const uint8_t *sig = p + MC_PUB_KEY_SIZE + 4;
        const uint8_t *appData = p + fixed;
        size_t appLen = len - fixed;
        if (appLen > MC_MAX_ADVERT_DATA) appLen = MC_MAX_ADVERT_DATA;

        // Verify the signature over pub_key || timestamp || app_data.
        uint8_t message[MC_PUB_KEY_SIZE + 4 + MC_MAX_ADVERT_DATA];
        int m = 0;
        memcpy(&message[m], pk, MC_PUB_KEY_SIZE); m += MC_PUB_KEY_SIZE;
        memcpy(&message[m], &ts, 4); m += 4;
        memcpy(&message[m], appData, appLen); m += appLen;
        if (!ed25519_verify(sig, message, m, pk)) return false;

        // Decode app_data for the name.
        uint8_t flags = appData[0];
        size_t i = 1;
        if (flags & MC_ADV_LATLON_MASK) i += 8;
        if (flags & 0x20) i += 2;
        if (flags & 0x40) i += 2;
        char nm[32] = {0};
        if ((flags & MC_ADV_NAME_MASK) && appLen > i)
        {
            size_t nlen = appLen - i;
            if (nlen > sizeof(nm) - 1) nlen = sizeof(nm) - 1;
            memcpy(nm, appData + i, nlen);
        }
        if (!nm[0]) return false;   // MeshCore drops nameless adverts

        bool durable = false;   // is this worth spending a flash write on?

        MCContact *c = findContact(pk);
        if (c)
        {
            if (ts <= c->lastAdvert) return true;   // replay guard
            if (strncmp(c->name, nm, sizeof(c->name)) != 0) durable = true;
        }
        else
        {
            c = allocContact();
            if (!c) return true;
            memcpy(c->pubKey, pk, MC_PUB_KEY_SIZE);
            c->used = true;
            durable = true;
            Serial.printf("  ✓ New MeshCore contact: %s\n", nm);
        }
        strncpy(c->name, nm, sizeof(c->name) - 1);
        c->lastAdvert = ts;
        c->lastRssi = (int16_t)rssi;

        // Nodes advertise on a timer, so persisting on every advert would rewrite
        // the whole ~1.1 KB blob hundreds of times a day purely to refresh an
        // RSSI. Write immediately when something durable changed; otherwise
        // throttle, so the replay-guard timestamps still survive a reboot without
        // hammering flash.
        unsigned long nowMs = millis();
        if (durable || nowMs - lastContactSave > CONTACT_SAVE_INTERVAL_MS)
        {
            lastContactSave = nowMs;
            saveContacts();
        }
        return true;
    }

    bool handleGroupText(const uint8_t *p, size_t len, int rssi)
    {
        if (len < 1 + MC_CIPHER_MAC_SIZE + 16) return false;
        if (p[0] != chanHash) return false;   // different channel

        uint8_t plain[208];
        int n = macThenDecrypt(chanSecret, plain, p + 1, len - 1);
        if (n < 5) return false;

        MCMessage msg = {};
        msg.timestamp = millis() / 1000;
        msg.rssi = (int16_t)rssi;
        msg.isDirect = false;
        // plaintext is [ts:4][txt_type:1]["name: text"]
        copyCString(msg.text, sizeof(msg.text), (const char *)&plain[5], n - 5);
        strncpy(msg.from, "(public)", sizeof(msg.from) - 1);
        push(msg);
        if (receiver) receiver(msg.from, msg.text, rssi, false);
        return true;
    }

    bool handleDirectText(const uint8_t *p, size_t len, int rssi,
                          bool isFlood, uint8_t rxPathLen, const uint8_t *rxPath)
    {
        if (len < 2 + MC_CIPHER_MAC_SIZE + 16) return false;
        if (p[0] != pubKey[0]) return false;   // not addressed to us

        // Try each known contact: the 1-byte src hash is ambiguous, the MAC decides.
        for (int i = 0; i < MC_MAX_CONTACTS; ++i)
        {
            if (!contacts[i].used) continue;
            if (contacts[i].pubKey[0] != p[1]) continue;

            uint8_t secret[MC_PUB_KEY_SIZE];
            ed25519_key_exchange(secret, contacts[i].pubKey, prvKey);

            uint8_t plain[208];
            int n = macThenDecrypt(secret, plain, p + 2, len - 2);
            if (n < 5) continue;

            MCMessage msg = {};
            msg.timestamp = millis() / 1000;
            msg.rssi = (int16_t)rssi;
            msg.isDirect = true;
            strncpy(msg.from, contacts[i].name, sizeof(msg.from) - 1);
            copyCString(msg.text, sizeof(msg.text), (const char *)&plain[5], n - 5);
            push(msg);
            if (receiver) receiver(msg.from, msg.text, rssi, true);

            // A flood-routed message means the sender has no route back yet, so
            // MeshCore answers with a PATH return that carries the path this
            // message travelled AND embeds the ACK. That is what lets the peer
            // switch from flooding to direct routing. A plain ACK still clears
            // the "transmit failed" state but teaches the sender nothing.
            if (isFlood)
                sendPathReturn(plain, n, contacts[i], secret, rxPathLen, rxPath);
            else
                sendAck(plain, n, contacts[i].pubKey);
            return true;
        }
        return false;
    }

    // Acknowledge a received direct message. Mirrors BaseChatMesh.cpp: the ACK
    // payload is a 6-byte proof-of-receipt whose first 4 bytes are
    // SHA256(plaintext[0 .. 5+text_len] || sender_pub_key), which is what the
    // sender compares against. Bytes 4 and 5 are an attempt marker and a random
    // byte, present only to keep the packet hash unique.
    void computeAckHash(const uint8_t *plain, int plainLen, const uint8_t *senderPub,
                        uint8_t out[6])
    {
        int textLen = 0;
        while (5 + textLen < plainLen && plain[5 + textLen] != '\0') textLen++;

        SHA256 sha;
        sha.reset();
        sha.update(plain, 5 + textLen);
        sha.update(senderPub, MC_PUB_KEY_SIZE);
        sha.finalize(out, 4);
        out[4] = (5 + textLen + 1 < plainLen) ? plain[5 + textLen + 1] : 0;
        out[5] = (uint8_t)(esp_random() & 0xFF);
    }

    // Reply to a flood-routed direct message with a PATH return: tells the peer
    // the route back to us and carries the ACK as its "extra" payload.
    //
    //   payload = [dest_hash][src_hash][ encryptThenMAC(
    //                 [path_len][path...][extra_type=ACK][ack_hash:6] ) ]
    void sendPathReturn(const uint8_t *plain, int plainLen, const MCContact &to,
                        const uint8_t *secret, uint8_t rxPathLen, const uint8_t *rxPath)
    {
        if (!sender) return;

        uint8_t ackHash[6];
        computeAckHash(plain, plainLen, to.pubKey, ackHash);

        uint8_t hashSize = (rxPathLen >> 6) + 1;
        uint8_t hashCount = rxPathLen & 63;
        size_t pathBytes = (size_t)hashCount * hashSize;

        uint8_t data[96];
        size_t d = 0;
        if (1 + pathBytes + 1 + sizeof(ackHash) > sizeof(data)) return;
        data[d++] = rxPathLen;
        memcpy(&data[d], rxPath, pathBytes); d += pathBytes;
        data[d++] = MC_PT_ACK;                       // extra_type
        memcpy(&data[d], ackHash, sizeof(ackHash)); d += sizeof(ackHash);

        uint8_t payload[160];
        int pl = 0;
        payload[pl++] = to.pubKey[0];   // dest hash
        payload[pl++] = pubKey[0];      // src hash
        int enc = encryptThenMAC(secret, &payload[pl], data, d);
        if (enc <= 0) return;
        pl += enc;

        uint8_t frame[200];
        size_t n = assemble(frame, sizeof(frame), MC_PT_PATH, MC_RT_FLOOD, payload, pl);
        if (n == 0) return;

        delay(200);   // TXT_ACK_DELAY
        if (sender(frame, n))
        {
            Serial.printf("   ↩ PATH return sent (%u path hop(s), ACK embedded)\n", hashCount);
        }
    }

    void sendAck(const uint8_t *plain, int plainLen, const uint8_t *senderPub)
    {
        if (!sender) return;

        uint8_t ackHash[6];
        computeAckHash(plain, plainLen, senderPub, ackHash);

        uint8_t frame[32];
        size_t n = assemble(frame, sizeof(frame), MC_PT_ACK, MC_RT_FLOOD, ackHash, sizeof(ackHash));
        if (n == 0) return;

        delay(200);   // MeshCore's TXT_ACK_DELAY: let the sender switch to RX
        if (sender(frame, n))
        {
            Serial.println(F("   ↩ ACK sent"));
        }
    }

    static void copyCString(char *dest, size_t destSize, const char *src, int maxLen)
    {
        int n = 0;
        while (n < maxLen && n < (int)destSize - 1 && src[n] != '\0') { dest[n] = src[n]; n++; }
        dest[n] = '\0';
    }

    MCContact *findContact(const uint8_t *pk)
    {
        for (int i = 0; i < MC_MAX_CONTACTS; ++i)
        {
            if (contacts[i].used && memcmp(contacts[i].pubKey, pk, MC_PUB_KEY_SIZE) == 0)
                return &contacts[i];
        }
        return NULL;
    }

    MCContact *allocContact()
    {
        for (int i = 0; i < MC_MAX_CONTACTS; ++i)
        {
            if (!contacts[i].used)
            {
                if (i + 1 > contactCount) contactCount = i + 1;
                return &contacts[i];
            }
        }
        return NULL;
    }
};

#endif // MESHCORE_PROTO_H
