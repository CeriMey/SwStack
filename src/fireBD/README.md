# fireBD (Firebase RTDB)

`fireBD/` is a lightweight Firebase Realtime Database (RTDB) layer built on the REST API (`.json`) so it works without the official Firebase SDK.

## Data layout (RTDB)

- `inbox/<toUserId>/<messageId>`: queued messages (consumed + deleted by the receiver)
- `statusQueue/<toUserId>/<eventId>`: queued delivery/read status events (consumed + deleted by the receiver)
- `users/<phone>`: demo account record (phone/pseudo/name + password hash)
- `userIndex/pseudo/<pseudoLower>`: pseudo -> phone mapping

Notes:
- Demo constraints: `phone` must contain at least 6 digits; `pseudoLower` is restricted to letters/digits/`_`/`-` (RTDB keys cannot contain `. # $ [ ]`).
- This is not Firebase Auth: for a real product, use Firebase Authentication + proper security rules (do not rely on open RTDB rules).

## WhatsApp example wiring

The `exemples/32-WhatsApp` demo uses these environment variables (or equivalent CLI flags):

- `SW_FIREBD_URL`: RTDB base URL (no trailing `/`), e.g. `https://<project>-default-rtdb.europe-west1.firebasedatabase.app`
- `SW_FIREBD_AUTH`: optional `auth=` token (database secret / ID token) depending on your RTDB rules
- `SW_FIREBD_UID`: user id used as the inbox/statusQueue key (must be the same across clients)
- `SW_FIREBD_POLL_MS`: poll interval (ms), default `1000`

CLI flags (they set the env vars at startup):

- `--firebd-url <url>`
- `--firebd-uid <uid>`
- `--firebd-auth <token>`
- `--firebd-poll-ms <ms>`
- `--profile <name>` (isolates local store + SwSettings for testing 2 instances)

The service is started when the WhatsApp demo becomes "logged in".
