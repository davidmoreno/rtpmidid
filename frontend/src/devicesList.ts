/** Parse `devices.list` RPC result. */

export type RegistryDevice = {
  identity: string;
  type: string;
  name: string;
  source: "discovered" | "ini" | "manual" | string;
  firstSeen: number;
  lastSeen: number;
  online: boolean;
  peerId?: number;
};

export type DevicesListResult = {
  enabled: boolean;
  devices: RegistryDevice[];
};

function num(v: unknown): number {
  if (typeof v === "number" && !Number.isNaN(v)) return v;
  if (typeof v === "string") {
    const n = Number(v);
    return Number.isNaN(n) ? 0 : n;
  }
  return 0;
}

export function parseDevicesListResult(raw: unknown): DevicesListResult {
  if (!raw || typeof raw !== "object") return { enabled: false, devices: [] };
  const o = raw as Record<string, unknown>;
  const enabled = o.enabled === true || o.enabled === 1;
  const rows = Array.isArray(o.devices) ? o.devices : [];
  const devices: RegistryDevice[] = [];
  for (const r of rows) {
    if (!r || typeof r !== "object") continue;
    const row = r as Record<string, unknown>;
    const identity = typeof row.identity === "string" ? row.identity : "";
    if (!identity) continue;
    const peerRaw = row.peer_id;
    const peerId =
      typeof peerRaw === "number"
        ? peerRaw
        : typeof peerRaw === "string"
          ? Number(peerRaw)
          : undefined;
    devices.push({
      identity,
      type: String(row.type ?? ""),
      name: String(row.name ?? ""),
      source: String(row.source ?? "discovered"),
      firstSeen: num(row.first_seen),
      lastSeen: num(row.last_seen),
      online: row.online === true || row.online === 1,
      peerId: Number.isFinite(peerId) ? peerId : undefined,
    });
  }
  return { enabled, devices };
}

export function formatLastSeen(ts: number): string {
  if (!ts) return "—";
  const d = new Date(ts * 1000);
  if (Number.isNaN(d.getTime())) return "—";
  return d.toLocaleString([], {
    month: "short",
    day: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  });
}

export function sourceLabel(source: string): string {
  switch (source) {
    case "discovered":
      return "Discovered";
    case "ini":
      return "Config";
    case "manual":
      return "Manual";
    default:
      return source;
  }
}
