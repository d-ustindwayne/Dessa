# dcrypto

Hashes, password storage, encryption, Base64 and hex, and secure random tokens.

**Install:** Comes with Dessa, nothing to install.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("dcrypto") { sha256, hashPassword, verifyPassword, encrypt, decrypt, token }

print(sha256("hello"));

var[Str] stored = hashPassword("s3cret");      // save this, not the password
print(verifyPassword("s3cret", stored));       // true

var[Str] secret = encrypt("meet at noon", "my password");
print(decrypt(secret, "my password"));
print(token(16));
```

## What it has

| Part | Names |
|---|---|
| Hashes, as hex | `md5`, `sha1`, `sha256`, `sha512`, `hmac(key, text)` (HMAC-SHA256) |
| Passwords | `hashPassword(password)`, `verifyPassword(password, stored)` |
| Encryption | `encrypt(text, password)`, `decrypt(data, password)` |
| Encoding | `toBase64`, `fromBase64`, `toHex`, `fromHex` |
| Secure random | `token(bytes)`, as hex |

`hashPassword` uses PBKDF2-SHA256 with a random salt. `encrypt` uses AES-256-GCM
with a key made from the password, so a wrong password or changed text makes
`decrypt` fail.

`md5` and `sha1` are for checksums only: don't use them for passwords or signatures.
