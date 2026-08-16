// Shared helpers: authenticated fetch against the Worker API, plus small UI utils.

const API = () => window.API_BASE.replace(/\/$/, "");

async function api(path, options = {}) {
  const res = await fetch(API() + path, {
    ...options,
    credentials: "include",
    headers: { "Content-Type": "application/json", ...(options.headers || {}) },
  });
  if (res.status === 401 && !location.pathname.endsWith("login.html")) {
    location.href = "login.html";
    throw new Error("unauthorized");
  }
  return res;
}

async function apiJson(path, options) {
  const res = await api(path, options);
  if (!res.ok) throw new Error((await res.json().catch(() => ({}))).error || res.statusText);
  return res.json();
}

function sendCommand(deviceId, type, payload = {}) {
  return apiJson(`/api/devices/${deviceId}/command`, {
    method: "POST",
    body: JSON.stringify({ type, payload }),
  });
}

function showToast(msg) {
  const t = document.getElementById("toast");
  if (!t) return;
  t.textContent = msg;
  t.classList.add("show");
  clearTimeout(t._t);
  t._t = setTimeout(() => t.classList.remove("show"), 2600);
}

const pad = (n) => String(n).padStart(2, "0");

// A device counts as online if it synced within the last 60s (sync interval is 10s).
function isOnline(lastSeenAt) {
  return lastSeenAt && Date.now() - lastSeenAt < 60000;
}

function relativeTime(ts) {
  if (!ts) return "never";
  const s = Math.floor((Date.now() - ts) / 1000);
  if (s < 60) return `${s}s ago`;
  if (s < 3600) return `${Math.floor(s / 60)}m ago`;
  if (s < 86400) return `${Math.floor(s / 3600)}h ago`;
  return `${Math.floor(s / 86400)}d ago`;
}
