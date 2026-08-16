// Small D1 query helpers. Keeps SQL out of the route handlers in index.ts.

export interface DeviceRow {
  id: string;
  name: string;
  api_key_hash: string;
  fw_version: string | null;
  created_at: number;
  last_seen_at: number | null;
}

export interface CommandRow {
  id: string;
  device_id: string;
  type: string;
  payload_json: string;
  status: string;
  created_at: number;
  acked_at: number | null;
}

export async function insertDevice(db: D1Database, id: string, name: string, apiKeyHash: string) {
  await db
    .prepare(`INSERT INTO devices (id, name, api_key_hash, created_at) VALUES (?, ?, ?, ?)`)
    .bind(id, name, apiKeyHash, Date.now())
    .run();
}

export async function getDevice(db: D1Database, id: string): Promise<DeviceRow | null> {
  const row = await db.prepare(`SELECT * FROM devices WHERE id = ?`).bind(id).first<DeviceRow>();
  return row ?? null;
}

export async function listDevicesWithStatus(db: D1Database) {
  const { results } = await db
    .prepare(
      `SELECT d.id, d.name, d.fw_version, d.created_at, d.last_seen_at,
              s.status_json, s.updated_at AS status_updated_at
       FROM devices d
       LEFT JOIN device_status s ON s.device_id = d.id
       ORDER BY d.created_at DESC`
    )
    .all();
  return results;
}

export async function touchDeviceSeen(db: D1Database, id: string, fwVersion: string | null) {
  await db
    .prepare(`UPDATE devices SET last_seen_at = ?, fw_version = COALESCE(?, fw_version) WHERE id = ?`)
    .bind(Date.now(), fwVersion, id)
    .run();
}

export async function upsertDeviceStatus(db: D1Database, deviceId: string, statusJson: string) {
  await db
    .prepare(
      `INSERT INTO device_status (device_id, status_json, updated_at) VALUES (?, ?, ?)
       ON CONFLICT(device_id) DO UPDATE SET status_json = excluded.status_json, updated_at = excluded.updated_at`
    )
    .bind(deviceId, statusJson, Date.now())
    .run();
}

export async function insertActivityLog(db: D1Database, deviceId: string, message: string) {
  await db
    .prepare(`INSERT INTO activity_log (device_id, message, created_at) VALUES (?, ?, ?)`)
    .bind(deviceId, message, Date.now())
    .run();
}

export async function getRecentLogs(db: D1Database, deviceId: string, limit = 120) {
  const { results } = await db
    .prepare(`SELECT message, created_at FROM activity_log WHERE device_id = ? ORDER BY created_at DESC LIMIT ?`)
    .bind(deviceId, limit)
    .all();
  return results;
}

export async function insertCommand(
  db: D1Database,
  id: string,
  deviceId: string,
  type: string,
  payloadJson: string
) {
  await db
    .prepare(
      `INSERT INTO commands (id, device_id, type, payload_json, status, created_at) VALUES (?, ?, ?, ?, 'pending', ?)`
    )
    .bind(id, deviceId, type, payloadJson, Date.now())
    .run();
}

export async function getPendingCommands(db: D1Database, deviceId: string): Promise<CommandRow[]> {
  const { results } = await db
    .prepare(`SELECT * FROM commands WHERE device_id = ? AND status = 'pending' ORDER BY created_at ASC`)
    .bind(deviceId)
    .all<CommandRow>();
  return results;
}

export async function markCommandsSent(db: D1Database, ids: string[]) {
  if (ids.length === 0) return;
  const placeholders = ids.map(() => "?").join(",");
  await db
    .prepare(`UPDATE commands SET status = 'sent' WHERE id IN (${placeholders})`)
    .bind(...ids)
    .run();
}

export async function ackCommand(db: D1Database, deviceId: string, commandId: string) {
  await db
    .prepare(`UPDATE commands SET status = 'done', acked_at = ? WHERE id = ? AND device_id = ?`)
    .bind(Date.now(), commandId, deviceId)
    .run();
}

export async function getLatestFirmware(db: D1Database) {
  return db
    .prepare(`SELECT version, r2_key, sha256, notes FROM firmware_versions ORDER BY created_at DESC LIMIT 1`)
    .first();
}
