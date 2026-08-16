// Smart Aquarium cloud relay -- Cloudflare Worker.
//
// Devices never receive inbound connections (they sit behind home NAT).
// Instead each ESP32 periodically POSTs its status and, in the same
// response, receives any commands the owner queued from the dashboard.
//
// Routes:
//   POST /api/auth/login                              owner login (passphrase -> cookie)
//   GET  /api/devices                                  owner: list devices + latest status
//   POST /api/devices                                  owner: register a new device
//   GET  /api/devices/:id/logs                         owner: recent activity log
//   POST /api/devices/:id/command                      owner: queue a command
//   GET  /api/firmware/latest                           owner: latest firmware version info
//   POST /api/devices/:id/status                       device: push status, receive pending commands
//   POST /api/devices/:id/commands/:cmdId/ack           device: acknowledge a command

import {
  Env,
  checkPassphrase,
  createSessionCookie,
  isOwnerAuthenticated,
  isDeviceAuthenticated,
  randomToken,
  sha256Hex,
} from "./auth";
import {
  ackCommand,
  getDevice,
  getLatestFirmware,
  getPendingCommands,
  getRecentLogs,
  insertActivityLog,
  insertCommand,
  insertDevice,
  listDevicesWithStatus,
  markCommandsSent,
  touchDeviceSeen,
  upsertDeviceStatus,
} from "./db";

const JSON_HEADERS = { "Content-Type": "application/json" };

function json(data: unknown, init: ResponseInit = {}): Response {
  return new Response(JSON.stringify(data), { ...init, headers: { ...JSON_HEADERS, ...init.headers } });
}

function corsHeaders(request: Request): HeadersInit {
  const origin = request.headers.get("Origin") || "*";
  return {
    "Access-Control-Allow-Origin": origin,
    "Access-Control-Allow-Credentials": "true",
    "Access-Control-Allow-Methods": "GET,POST,OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, X-Device-Key",
    Vary: "Origin",
  };
}

async function requireOwner(request: Request, env: Env): Promise<Response | null> {
  if (await isOwnerAuthenticated(request, env)) return null;
  return json({ error: "unauthorized" }, { status: 401 });
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const cors = corsHeaders(request);
    if (request.method === "OPTIONS") {
      return new Response(null, { headers: cors });
    }

    const response = await route(request, env);
    for (const [k, v] of Object.entries(cors)) response.headers.set(k, v);
    return response;
  },
};

async function route(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);
  const path = url.pathname;
  const method = request.method;

  // ---- auth ----
  if (path === "/api/auth/login" && method === "POST") {
    const body = await request.json<{ passphrase?: string }>().catch(() => ({}));
    if (!body.passphrase || !checkPassphrase(env, body.passphrase)) {
      return json({ error: "invalid passphrase" }, { status: 401 });
    }
    const cookie = await createSessionCookie(env);
    return json({ ok: true }, { headers: { "Set-Cookie": cookie } });
  }

  // ---- owner: device registry ----
  if (path === "/api/devices" && method === "GET") {
    const unauthorized = await requireOwner(request, env);
    if (unauthorized) return unauthorized;
    const devices = await listDevicesWithStatus(env.DB);
    return json({ devices });
  }

  if (path === "/api/devices" && method === "POST") {
    const unauthorized = await requireOwner(request, env);
    if (unauthorized) return unauthorized;
    const body = await request.json<{ name?: string }>().catch(() => ({}));
    const name = (body.name || "Unnamed Aquarium").slice(0, 80);
    const id = crypto.randomUUID();
    const apiKey = randomToken(32);
    const apiKeyHash = await sha256Hex(apiKey);
    await insertDevice(env.DB, id, name, apiKeyHash);
    // apiKey is returned once -- the frontend must show it to the user
    // immediately so they can enter it into the ESP32's Settings tab.
    return json({ id, name, apiKey });
  }

  const logsMatch = path.match(/^\/api\/devices\/([^/]+)\/logs$/);
  if (logsMatch && method === "GET") {
    const unauthorized = await requireOwner(request, env);
    if (unauthorized) return unauthorized;
    const logs = await getRecentLogs(env.DB, logsMatch[1]);
    return json({ logs });
  }

  const commandMatch = path.match(/^\/api\/devices\/([^/]+)\/command$/);
  if (commandMatch && method === "POST") {
    const unauthorized = await requireOwner(request, env);
    if (unauthorized) return unauthorized;
    const deviceId = commandMatch[1];
    const device = await getDevice(env.DB, deviceId);
    if (!device) return json({ error: "device not found" }, { status: 404 });

    const body = await request.json<{ type?: string; payload?: unknown }>().catch(() => ({}));
    const validTypes = ["relay", "feed", "set_schedule", "set_pump_timer", "reboot", "ota"];
    if (!body.type || !validTypes.includes(body.type)) {
      return json({ error: "invalid command type" }, { status: 400 });
    }
    const commandId = crypto.randomUUID();
    await insertCommand(env.DB, commandId, deviceId, body.type, JSON.stringify(body.payload ?? {}));
    return json({ id: commandId, status: "pending" });
  }

  if (path === "/api/firmware/latest" && method === "GET") {
    const unauthorized = await requireOwner(request, env);
    if (unauthorized) return unauthorized;
    const fw = await getLatestFirmware(env.DB);
    if (!fw) return json({ error: "no firmware uploaded" }, { status: 404 });
    return json({ ...fw, url: `${url.origin}/api/firmware/${fw.version}/download` });
  }

  // Firmware binaries are served unauthenticated: ESP32's HTTPUpdate can't
  // easily attach custom headers, and a compiled .bin isn't a secret.
  const downloadMatch = path.match(/^\/api\/firmware\/([^/]+)\/download$/);
  if (downloadMatch && method === "GET") {
    const row = await env.DB.prepare(`SELECT r2_key FROM firmware_versions WHERE version = ?`)
      .bind(downloadMatch[1])
      .first<{ r2_key: string }>();
    if (!row) return json({ error: "version not found" }, { status: 404 });
    const object = await env.FIRMWARE.get(row.r2_key);
    if (!object) return json({ error: "binary missing from storage" }, { status: 404 });
    return new Response(object.body, {
      headers: {
        "Content-Type": "application/octet-stream",
        "Content-Length": String(object.size),
      },
    });
  }

  // ---- device: status push + command poll (combined in one round trip) ----
  const statusMatch = path.match(/^\/api\/devices\/([^/]+)\/status$/);
  if (statusMatch && method === "POST") {
    const deviceId = statusMatch[1];
    const device = await getDevice(env.DB, deviceId);
    if (!device) return json({ error: "device not found" }, { status: 404 });
    if (!(await isDeviceAuthenticated(request, device.api_key_hash))) {
      return json({ error: "unauthorized" }, { status: 401 });
    }

    const statusBody = await request.text();
    await upsertDeviceStatus(env.DB, deviceId, statusBody);

    let fwVersion: string | null = null;
    try {
      fwVersion = JSON.parse(statusBody)?.fw ?? null;
    } catch {
      /* status payload wasn't valid JSON -- ignore, still recorded above */
    }
    await touchDeviceSeen(env.DB, deviceId, fwVersion);
    await insertActivityLog(env.DB, deviceId, "Cloud sync: status received");

    const pending = await getPendingCommands(env.DB, deviceId);
    if (pending.length > 0) {
      await markCommandsSent(env.DB, pending.map((c) => c.id));
    }

    return json({
      commands: pending.map((c) => ({ id: c.id, type: c.type, payload: JSON.parse(c.payload_json) })),
    });
  }

  const ackMatch = path.match(/^\/api\/devices\/([^/]+)\/commands\/([^/]+)\/ack$/);
  if (ackMatch && method === "POST") {
    const [, deviceId, commandId] = ackMatch;
    const device = await getDevice(env.DB, deviceId);
    if (!device) return json({ error: "device not found" }, { status: 404 });
    if (!(await isDeviceAuthenticated(request, device.api_key_hash))) {
      return json({ error: "unauthorized" }, { status: 401 });
    }
    await ackCommand(env.DB, deviceId, commandId);
    return json({ ok: true });
  }

  return json({ error: "not found" }, { status: 404 });
}
