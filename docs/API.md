# Gateway HTTP API

Every endpoint requires HTTP Basic auth: user `admin`, password = the admin
password from serial menu `10`. The server refuses to start at all if no admin
password is set.

> **Plain HTTP on the LAN.** Credentials and message bodies cross the network
> readable. Fine on a home network; put it behind a reverse proxy with TLS if it
> needs to be reachable from anywhere else.

Base URL is the gateway's IP, printed on boot and shown in the syslog `boot` line.

```bash
GW=http://192.168.15.66
AUTH='-u admin:YOUR_ADMIN_PASSWORD'
```

---

## Messages

### `GET /api/messages`

Recent messages, newest first. Includes both received and sent.

```bash
curl $AUTH "$GW/api/messages?limit=10"
```

```json
[
  {"from":"(public)","text":"pager: hello","rssi":-48,
   "direct":false,"outgoing":false,"ts":1421}
]
```

`limit` defaults to 20, capped at the on-device ring buffer size (24).

### `POST /api/messages`

Send a message. `to` is a contact `id` from `/api/contacts`, or `-1` for the
public channel.

```bash
# public channel
curl $AUTH -X POST "$GW/api/messages" \
     -H 'Content-Type: application/json' \
     -d '{"text":"hello mesh","to":-1}'

# direct to contact 0
curl $AUTH -X POST "$GW/api/messages" \
     -H 'Content-Type: application/json' \
     -d '{"text":"just for you","to":0}'
```

Returns `{"ok":true}`, or `400` for empty text / unknown contact, `500` if the
radio refused the transmit.

The gateway builds a real MeshCore frame — AES-128 payload, HMAC, correct
routing header — so real nodes accept it. Public messages use the channel PSK;
direct messages derive a per-contact secret via `ed25519_key_exchange`.

---

## Contacts

### `GET /api/contacts`

Nodes learned from signature-verified adverts.

```bash
curl $AUTH "$GW/api/contacts"
```

```json
[{"id":0,"name":"Pager One","rssi":-48,"hash":"0x3C","lastAdvert":1600000042}]
```

`id` is the index to pass as `to` when sending. `hash` is the first byte of the
node's public key, which is what MeshCore uses to address it.

Contacts appear on their own as nodes advertise; there is no "add contact" call.
To make *this* gateway addressable by others, broadcast an advert:

```bash
curl $AUTH -X POST "$GW/api/advert"
```

---

## Webhook

Registers a URL that receives a POST for every decoded inbound message.

> Also editable from the **Settings → Webhook** panel in the web UI, including a
> "Send test delivery" button and live delivery counters. The API below is for
> scripting; nothing here requires it.

**The token is not issued by the gateway — you choose it.** It authenticates the
gateway *to your endpoint*, the opposite direction to the admin password:

| Secret | Proves | Direction |
|---|---|---|
| admin password | you may call this API | you → gateway |
| webhook token | the POST really came from this gateway | gateway → your server |

Generate one with `openssl rand -hex 32`, set it below, and have your endpoint
compare the `Authorization: Bearer` header. It is write-only — `GET` reports only
`hasToken`, so if you lose it, set a new one.

### `GET /api/webhook`

```json
{"enabled":true,"url":"https://example.com/hook","hasToken":true,
 "includePublic":true,"includeDirect":true,
 "delivered":12,"failed":0,"dropped":0,"pending":0}
```

The token itself is never returned — only whether one is set.

### `POST /api/webhook`

```bash
curl $AUTH -X POST "$GW/api/webhook" \
     -H 'Content-Type: application/json' \
     -d '{"url":"https://example.com/hook","token":"s3cret",
          "enabled":true,"includePublic":true,"includeDirect":true}'
```

An empty or omitted `token` leaves the stored one unchanged. Clearing `url`
disables the webhook. Settings persist across reboots.

### `POST /api/webhook/test`

Sends a synthetic delivery immediately so you can verify the endpoint without
waiting for mesh traffic. Returns `502` if the endpoint did not return 2xx.

### Delivery format

```http
POST /your/hook
Content-Type: application/json
Authorization: Bearer <token>      (only if a token is set)
```

```json
{"node":"MQTT-Gateway","from":"Pager One","text":"hello",
 "rssi":-48,"direct":true,"ts":1600000123}
```

### Delivery behaviour

- **Queued, not inline.** An HTTP POST to an unreachable host can block for
  seconds; sending from the packet handler would make the radio miss traffic for
  the whole of it. Events are queued on receipt and drained one per loop pass.
- **8-slot queue, oldest dropped when full.** A slow endpoint degrades delivery
  rather than stalling the mesh. `dropped` in `GET /api/webhook` counts losses.
- **No retries.** A failed POST increments `failed` and is discarded. The mesh is
  the system of record; the webhook is a convenience.
- **Queued while WiFi is down**, drained when it returns.
- 4-second connect and response timeout.

---

## Configuration

### `GET /api/config` · `POST /api/config`

Read and update LoRa, node, WiFi, MQTT, logging and location settings. Passwords
are never returned — only `hasPassword` flags — and an empty password field on
POST means "leave unchanged".

The response includes `"restart":true` when the change needs a reboot: LoRa,
WiFi, MQTT and logging are all initialised once in `setup()`.

```bash
curl $AUTH "$GW/api/config"
curl $AUTH -X POST "$GW/api/config" \
     -H 'Content-Type: application/json' \
     -d '{"lora":{"spreadingFactor":8},"repeater":{"maxHops":3}}'
```

Numeric values are clamped on the device (SF 7-12, CR 5-8, TX 2-22, hops 0-63).

### `POST /api/restart`

Reboots. Responds first, then restarts, so the caller sees `{"ok":true}`.

---

## Other

| Endpoint | Purpose |
|---|---|
| `GET /api/state` | Combined identity + contacts + messages, used by the web page |
| `POST /api/send` | Older alias of `POST /api/messages` |
| `POST /api/advert` | Broadcast a signed self-advert |
