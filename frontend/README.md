# Smart Aquarium — Cloud Dashboard (Netlify)

Static site that talks to the Cloudflare Worker in [`../backend`](../backend). No build step,
no framework — plain HTML/CSS/JS, same visual design as the on-device dashboard.

| File | Purpose |
|---|---|
| `login.html` | Passphrase sign-in |
| `index.html` | Device list + register new device |
| `device.html` | Per-device control (Home / Relays / Logs / Settings + OTA) |
| `app.js` | Shared API helpers |
| `config.js` | **Set your Worker URL here** |
| `style.css` | Shared styles |

## Setup

1. Deploy the backend first (see `../backend/README.md`) and note its URL.
2. Edit `config.js`:
   ```js
   window.API_BASE = "https://smart-aquarium-api.your-subdomain.workers.dev";
   ```
3. Deploy:
   ```bash
   npx netlify deploy --prod --dir=frontend
   ```
   Or connect the repo in the Netlify UI with **base directory** `frontend` and no build command.

## Adding an aquarium

1. Open the site, sign in with your passphrase.
2. Type a name → **Register New Device** → copy the **Device ID** and **API Key** shown
   (the key is displayed only once).
3. On the ESP32's *local* dashboard (`http://<esp32-ip>`), go to **Settings → Cloud** and enter
   the API URL, Device ID, and API Key, then save.
4. Within ~10 seconds the device appears as **online** in the cloud dashboard.

Repeat for as many ESP32 units as you like — each gets its own ID and key.

## How control works

The ESP32 can't accept inbound connections from the internet (it's behind your router's NAT), so
control is queued rather than immediate: pressing a toggle inserts a command, and the device
applies it on its next sync (~10s). Status shown in the dashboard is likewise the last snapshot
the device pushed. For instant response, use the device's local dashboard on your home WiFi.

## Local development

```bash
npx serve frontend      # or any static file server
```
Set `window.API_BASE = "http://localhost:8787"` in `config.js` while running `wrangler dev`.
