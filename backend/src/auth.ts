// Auth helpers: owner (passphrase -> signed cookie) and per-device API keys.
// No external deps -- everything here uses the Workers-native Web Crypto API.

export interface Env {
  DB: D1Database;
  FIRMWARE: R2Bucket;
  OWNER_PASSPHRASE: string;
  COOKIE_SECRET: string;
}

const COOKIE_NAME = "aq_session";
const SESSION_TTL_MS = 30 * 24 * 60 * 60 * 1000; // 30 days

function toHex(buf: ArrayBuffer): string {
  return [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export async function sha256Hex(input: string): Promise<string> {
  const data = new TextEncoder().encode(input);
  const digest = await crypto.subtle.digest("SHA-256", data);
  return toHex(digest);
}

async function hmacSha256Hex(secret: string, message: string): Promise<string> {
  const key = await crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"]
  );
  const sig = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(message));
  return toHex(sig);
}

function timingSafeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

export function randomToken(bytes = 32): string {
  const arr = new Uint8Array(bytes);
  crypto.getRandomValues(arr);
  return toHex(arr.buffer);
}

/** Checks the owner passphrase in constant time relative to the stored value's length. */
export function checkPassphrase(env: Env, supplied: string): boolean {
  return timingSafeEqual(supplied, env.OWNER_PASSPHRASE);
}

/** Signs `expiresAt.HMAC` into a cookie value. */
export async function createSessionCookie(env: Env): Promise<string> {
  const expiresAt = Date.now() + SESSION_TTL_MS;
  const sig = await hmacSha256Hex(env.COOKIE_SECRET, String(expiresAt));
  const value = `${expiresAt}.${sig}`;
  return `${COOKIE_NAME}=${value}; Path=/; HttpOnly; Secure; SameSite=None; Max-Age=${SESSION_TTL_MS / 1000}`;
}

function readCookie(request: Request, name: string): string | null {
  const header = request.headers.get("Cookie");
  if (!header) return null;
  for (const part of header.split(";")) {
    const [k, ...rest] = part.trim().split("=");
    if (k === name) return rest.join("=");
  }
  return null;
}

export async function isOwnerAuthenticated(request: Request, env: Env): Promise<boolean> {
  const value = readCookie(request, COOKIE_NAME);
  if (!value) return false;
  const [expiresAtStr, sig] = value.split(".");
  if (!expiresAtStr || !sig) return false;
  const expiresAt = Number(expiresAtStr);
  if (!Number.isFinite(expiresAt) || Date.now() > expiresAt) return false;
  const expectedSig = await hmacSha256Hex(env.COOKIE_SECRET, expiresAtStr);
  return timingSafeEqual(sig, expectedSig);
}

/** Validates the X-Device-Key header against the stored hash for that device. */
export async function isDeviceAuthenticated(
  request: Request,
  storedHash: string
): Promise<boolean> {
  const key = request.headers.get("X-Device-Key");
  if (!key) return false;
  const hash = await sha256Hex(key);
  return timingSafeEqual(hash, storedHash);
}
