-- Devices registered by the owner. api_key_hash is sha256(hex) of the raw
-- device API key -- the raw key is only ever shown once, at creation time.
CREATE TABLE devices (
  id            TEXT PRIMARY KEY,
  name          TEXT NOT NULL,
  api_key_hash  TEXT NOT NULL,
  fw_version    TEXT,
  created_at    INTEGER NOT NULL,
  last_seen_at  INTEGER
);

-- Latest status snapshot per device, stored as a single JSON blob so new
-- fields (sensors, future features) don't need a migration to add.
CREATE TABLE device_status (
  device_id   TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,
  status_json TEXT NOT NULL,
  updated_at  INTEGER NOT NULL
);

-- Command queue. Frontend inserts rows with status='pending'; a device's
-- next /status push marks them 'sent'; the device's ack marks them 'done'.
CREATE TABLE commands (
  id           TEXT PRIMARY KEY,
  device_id    TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  type         TEXT NOT NULL, -- relay | feed | set_schedule | set_pump_timer | ota
  payload_json TEXT NOT NULL,
  status       TEXT NOT NULL DEFAULT 'pending', -- pending | sent | done
  created_at   INTEGER NOT NULL,
  acked_at     INTEGER
);
CREATE INDEX idx_commands_device_status ON commands(device_id, status);

CREATE TABLE activity_log (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id  TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  message    TEXT NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE INDEX idx_activity_log_device ON activity_log(device_id, created_at DESC);

CREATE TABLE firmware_versions (
  version    TEXT PRIMARY KEY,
  r2_key     TEXT NOT NULL,
  sha256     TEXT NOT NULL,
  notes      TEXT,
  created_at INTEGER NOT NULL
);
