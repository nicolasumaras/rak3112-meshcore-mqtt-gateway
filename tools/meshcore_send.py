#!/usr/bin/env python3
"""Send a real MeshCore group-text message through the RAK3112 MQTT gateway.

The gateway forwards the payload of `{prefix}/commands/send` to the radio
verbatim, so publishing a correctly-formed MeshCore frame there transmits a
packet that real MeshCore nodes accept. The firmware itself needs no MeshCore
protocol support: the framing and crypto happen here.

Frame layout, from meshcore-dev/MeshCore (src/Packet.cpp, src/Mesh.cpp,
src/Utils.cpp, src/helpers/BaseChatMesh.cpp):

    [header:1][path_len:1][channel_hash:1][MAC:2][ciphertext:N]

    header      (PAYLOAD_TYPE_GRP_TXT << 2) | ROUTE_TYPE_FLOOD  = 0x15
    path_len    ((hash_size - 1) << 6) | hash_count             = 0x00 when new
    plaintext   [timestamp:4 LE][txt_type:1 = 0 plain]["<sender>: <text>"]
    ciphertext  AES-128-ECB, zero-padded to 16-byte blocks, key = secret[0:16]
    MAC         HMAC-SHA256(secret[0:32], ciphertext)[0:2]
    chan hash   SHA256(psk)[0]

Verified against live traffic: the channel hash computed from the default public
PSK is 0x11, which is exactly the byte observed in packets captured from real
LilyGo MeshCore pagers.

Usage:
    python3 meshcore_send.py "sender name" "message text"
    python3 meshcore_send.py --broker mybroker.local --prefix MYPREFIX "me" "hi"

Requires: paho-mqtt, pycryptodome
"""
import argparse
import base64
import hashlib
import hmac
import json
import struct
import sys
import time

import paho.mqtt.client as mqtt
from Crypto.Cipher import AES

# MeshCore's default "Public" channel pre-shared key.
DEFAULT_PSK_B64 = "izOH6cXN6mrJ5e26oRXNcg=="

PAYLOAD_TYPE_GRP_TXT = 0x05
ROUTE_TYPE_FLOOD = 0x01
TXT_TYPE_PLAIN = 0x00


def channel_material(psk_b64):
    psk = base64.b64decode(psk_b64)
    if len(psk) not in (16, 32):
        raise ValueError("PSK must decode to 16 or 32 bytes, got %d" % len(psk))
    # addChannel() zeroes a 32-byte secret then copies the PSK into the front,
    # and hashes only the PSK-length prefix.
    secret32 = psk.ljust(32, b"\x00")
    chan_hash = hashlib.sha256(psk).digest()[:1]
    return psk, secret32, chan_hash


def build_grp_txt(sender, text, psk_b64=DEFAULT_PSK_B64, timestamp=None):
    psk, secret32, chan_hash = channel_material(psk_b64)
    if timestamp is None:
        timestamp = int(time.time())

    plain = struct.pack("<I", timestamp) + bytes([TXT_TYPE_PLAIN])
    plain += ("%s: %s" % (sender, text)).encode("utf-8")
    plain += b"\x00" * ((-len(plain)) % 16)          # zero pad, as Utils::encrypt does

    ciphertext = AES.new(psk[:16], AES.MODE_ECB).encrypt(plain)
    mac = hmac.new(secret32, ciphertext, hashlib.sha256).digest()[:2]

    header = (PAYLOAD_TYPE_GRP_TXT << 2) | ROUTE_TYPE_FLOOD
    return bytes([header, 0x00]) + chan_hash + mac + ciphertext


def decode_grp_txt(frame, psk_b64=DEFAULT_PSK_B64):
    """Parse a frame back the way a receiving node would. Used as a self-check."""
    psk, secret32, chan_hash = channel_material(psk_b64)
    header = frame[0]
    path_len = frame[1]
    offset = 2 + (path_len & 63) * ((path_len >> 6) + 1)
    payload = frame[offset:]
    got_hash, mac, ciphertext = payload[0:1], payload[1:3], payload[3:]

    plain = AES.new(psk[:16], AES.MODE_ECB).decrypt(ciphertext)
    return {
        "payload_type": (header >> 2) & 0x0F,
        "route_type": header & 0x03,
        "channel_ok": got_hash == chan_hash,
        "mac_ok": hmac.new(secret32, ciphertext, hashlib.sha256).digest()[:2] == mac,
        "timestamp": struct.unpack("<I", plain[0:4])[0],
        "text": plain[5:].split(b"\x00")[0].decode("utf-8", "replace"),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sender", help="sender name shown to receiving nodes")
    ap.add_argument("text", help="message body")
    ap.add_argument("--broker", default="test.mosquitto.org")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--prefix", default="MESHCORE-RAK3112-7075DC3C",
                    help="gateway topic prefix")
    ap.add_argument("--psk", default=DEFAULT_PSK_B64,
                    help="channel PSK, base64 (default: MeshCore public channel)")
    ap.add_argument("--dry-run", action="store_true",
                    help="build and verify the frame without publishing")
    args = ap.parse_args()

    frame = build_grp_txt(args.sender, args.text, args.psk)
    check = decode_grp_txt(frame, args.psk)

    print("frame (%d bytes): %s" % (len(frame), frame.hex().upper()))
    print("self-check     : %s" % json.dumps(check))
    if not (check["channel_ok"] and check["mac_ok"]):
        print("refusing to send: frame failed its own validation", file=sys.stderr)
        return 1
    if args.dry_run:
        return 0

    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except Exception:
        client = mqtt.Client()
    client.connect(args.broker, args.port, 60)
    client.loop_start()
    time.sleep(2)

    # commands/send forwards the payload to the radio verbatim.
    # NB: the {prefix}/raw JSON path is gated behind config.mqtt.bridgeAll and
    # was observed silently not transmitting - see the repo issue tracker.
    topic = "%s/commands/send" % args.prefix
    client.publish(topic, frame, qos=0)
    print("published %d bytes to %s" % (len(frame), topic))
    time.sleep(3)
    client.loop_stop()
    client.disconnect()
    return 0


if __name__ == "__main__":
    sys.exit(main())
