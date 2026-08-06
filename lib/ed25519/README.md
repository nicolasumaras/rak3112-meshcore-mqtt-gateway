# ed25519 (vendored)

Orson Peters' portable Ed25519 implementation — <https://github.com/orlp/ed25519>.
zlib licence, see `license.txt`. Unmodified.

Vendored rather than pulled from the PlatformIO registry because
`meshcore-dev/MeshCore` vendors this exact library (`lib/ed25519/`), and MeshCore
interoperability depends on matching its key handling byte-for-byte:

- public key 32 bytes, private key 64 bytes
- `ed25519_key_exchange()` produces the shared secret used for direct messages
- `ed25519_sign()` / `ed25519_verify()` cover advert signatures

Note this is a *different* library from `rweather/Crypto`'s `Ed25519.h`, which
MeshCore also includes. Both are needed: `rweather/Crypto` supplies AES128 and
SHA256/HMAC, this supplies the Ed25519 keypair, signing and X25519 key exchange.
