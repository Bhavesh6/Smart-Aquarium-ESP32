# Smart Aquarium — Cloud Backend (Cloudflare Worker)

Relay between your ESP32 devices and the hosted dashboard. Devices push status and poll for
commands; the dashboard reads status and queues commands. Free tier is plenty for this.

## What's here

| File | Purpose |
|---|---|
| `src/index.ts` | HTTP router — all API routes |
| `src/auth.ts` | Owner passphrase + session cookie, device API key checks |
| `src/db.ts` | D1 query helpers |
| `migrations/0001_init.sql` | Database schema |
| `wrangler.toml` | Worker config (D1 + R2 bindings) |

## One-time setup

```bash
cd backend
npm install
npx wrangler login
```

**Create the database:**
```bash
npx wrangler d1 create smart-aquarium-db
```
Copy the printed `database_id` into `wrangler.toml`.

**Create the firmware bucket (for OTA):**
```bash
npx wrangler r2 bucket create smart-aquarium-firmware
```

**Set secrets:**
```bash
npx wrangler secret put OWNER_PASSPHRASE   # the password you'll use to log into the dashboard
npx wrangler secret put COOKIE_SECRET      # any long random string
```

**Apply migrations:**
```bash
npm run migrate:remote
```

**Deploy:**
```bash
npm run deploy
```

Note the deployed URL (e.g. `https://smart-aquarium-api.<you>.workers.dev`) — you need it for both
the frontend config and each ESP32's Settings tab.

## Local development

```bash
npm run migrate:local
npm run dev
```
Runs at `http://localhost:8787` against a local D1 instance. Set local secrets by creating a
`.dev.vars` file (gitignored):
```
OWNER_PASSPHRASE=test123
COOKIE_SECRET=localdevsecret
```

## API reference

### Owner endpoints (session cookie required)

| Method | Path | Description |
|---|---|---|
| POST | `/api/auth/login` | Body `{"passphrase":"..."}` → sets session cookie |
| GET | `/api/devices` | List all devices with their latest status |
| POST | `/api/devices` | Body `{"name":"..."}` → registers device, returns `{id, apiKey}` **once** |
| GET | `/api/devices/:id/logs` | Recent activity log entries |
| POST | `/api/devices/:id/command` | Body `{"type":"...","payload":{...}}` → queues a command |
| GET | `/api/firmware/latest` | Latest firmware version + download URL |

**Command types and payloads:**
```jsonc
{"type": "relay",          "payload": {"id": 1, "on": true}}
{"type": "feed",           "payload": {}}
{"type": "set_schedule",   "payload": {"mh": 10, "mm": 0, "nh": 22, "nm": 0}}
{"type": "set_pump_timer", "payload": {"on": 60, "off": 60, "enabled": true}}
{"type": "reboot",         "payload": {}}
{"type": "ota",            "payload": {"url": "https://.../api/firmware/1.1.0/download", "version": "1.1.0"}}
```

### Device endpoints (`X-Device-Key` header required)

| Method | Path | Description |
|---|---|---|
| POST | `/api/devices/:id/status` | Body = device status JSON. Response contains any pending commands. |
| POST | `/api/devices/:id/commands/:cmdId/ack` | Marks a command as executed |

### Public

| Method | Path | Description |
|---|---|---|
| GET | `/api/firmware/:version/download` | Streams the firmware binary (used by ESP32 OTA) |

## Publishing a firmware update (OTA)

1. In Arduino IDE, bump `FW_VERSION` in `smart_aquarium.ino`, then `Sketch → Export Compiled Binary`.
2. Upload the `.bin` to R2 and register it:

```bash
VERSION=1.1.0
npx wrangler r2 object put smart-aquarium-firmware/firmware-$VERSION.bin \
  --file ./smart_aquarium.ino.bin

SHA=$(sha256sum ./smart_aquarium.ino.bin | cut -d' ' -f1)
npx wrangler d1 execute smart-aquarium-db --remote --command \
  "INSERT INTO firmware_versions (version, r2_key, sha256, notes, created_at) \
   VALUES ('$VERSION', 'firmware-$VERSION.bin', '$SHA', 'What changed', $(date +%s000));"
```

3. Open the device page in the dashboard → **Firmware** → **Update**. The command is queued; the
   ESP32 picks it up on its next sync, downloads, flashes, and reboots.

There's deliberately no upload UI — publishing firmware is rare and this keeps the Worker small.
