/** A device known to the daemon, keyed by identity string. */
export type DeviceRow = {
  identity: string;
  /** Derived: type prefix from identity ("alsa_seq" | "rawmidi"). */
  type: string;
  /** Derived: display label. */
  label: string;
  /** ALSA only: client name from identity. */
  client_name?: string;
  /** ALSA only: port name from identity. */
  port_name?: string;
  /** ALSA only: numeric client id (0 if from identity-only event). */
  client?: number;
  /** ALSA only: numeric port id (0 if from identity-only event). */
  port?: number;
};

/** Legacy alias — kept for gradual migration. */
export type MidiAlsaSeqEntry = DeviceRow;
export type MidiRawmidiEntry = DeviceRow;

/** Pick for mDNS Connect: create `peer_device_alsa_seq_t` or `local_rawmidi_t` then wire RTP client. */
export type WireLocalChoice =
  | {
      mode: "alsa_seq";
      listingLabel: string;
      /** Enumerated ALSA source; daemon subscribes this client:port → new rtpmidid port. */
      client: number;
      port: number;
    }
  | { mode: "rawmidi"; device: string; listingLabel: string };

/** RTP-MIDI / mDNS UDP port string for `peer_device_rtpmidi_client_t` (default Apple 5004). */
export function normalizeRtpMidiUdpPort(p: number | string): string {
  const n = typeof p === "number" ? p : Number(String(p).trim());
  if (!Number.isFinite(n) || n < 1 || n > 65535) return "5004";
  return String(Math.floor(n));
}

export function parseDeviceList(r: unknown): DeviceRow[] | null {
  if (r && typeof r === "object" && !Array.isArray(r) && "error" in r) {
    console.error("device list error:", r);
    return null;
  }
  if (!Array.isArray(r)) {
    console.error("device list result is not an array:", typeof r, r);
    return null;
  }
  const out: DeviceRow[] = [];
  for (const item of r) {
    const identity = typeof item === "string" ? item : String((item as Record<string, unknown>).identity ?? "");
    if (!identity) continue;
    const device = deviceFromIdentity(identity);
    if (device) out.push(device);
  }
  return out;
}

/** Build a DeviceRow from an identity string.
 *  Handles the `alsa_seq:c=X,p=Y[,client=Name,port=Name]` format
 *  produced by aseq_t::port_identity(). */
export function deviceFromIdentity(identity: string): DeviceRow | null {
  const colon = identity.indexOf(":");
  if (colon <= 0) return null;
  const type = identity.slice(0, colon);
  const fields = parseIdentityFields(identity.slice(colon + 1));

  if (type === "alsa_seq") {
    const cn = fields["client"] ?? "";
    const pn = fields["port"] ?? "";
    const cid = parseIntOrZero(fields["c"]);
    const pid = parseIntOrZero(fields["p"]);
    const label = cn || pn
      ? (cn === pn ? cn : `${cn} · ${pn}`)
      : `ALSA ${fields["c"]}:${fields["p"] ?? "0"}`;
    return {
      identity,
      type: "alsa_seq",
      label,
      client_name: cn || undefined,
      port_name: pn || undefined,
      client: cid || undefined,
      port: pid || undefined,
    };
  }
  if (type === "rawmidi") {
    const dev = fields["device"] ?? identity;
    return { identity, type: "rawmidi", label: dev };
  }
  return null;
}

function parseIntOrZero(s: string | undefined): number {
  if (!s) return 0;
  const n = parseInt(s, 10);
  return Number.isFinite(n) ? n : 0;
}

function parseIdentityFields(tail: string): Record<string, string> {
  const fields: Record<string, string> = {};
  for (const part of tail.split(",")) {
    const eq = part.indexOf("=");
    if (eq <= 0) continue;
    fields[part.slice(0, eq)] = part.slice(eq + 1);
  }
  return fields;
}

/** Pick local (ephemeral) ALSA/rawmidi rows from the unified `devices.list` result. */
export function filterLocalDevices(devices: Array<{ identity: string; type?: string; source?: string }>): DeviceRow[] {
  const out: DeviceRow[] = [];
  for (const row of devices) {
    if (row.source !== "local") continue;
    const device = deviceFromIdentity(row.identity);
    if (device) out.push(device);
  }
  return out;
}

/** Backward-compat: parse legacy midi.listAlsaSeq array. */
export function parseMidiAlsaSeqResult(r: unknown): DeviceRow[] | null {
  return parseDeviceList(r);
}

/** Backward-compat: parse legacy midi.listRawMidi array. */
export function parseMidiRawmidiResult(r: unknown): DeviceRow[] | null {
  return parseDeviceList(r);
}

/** Safe peer name for router.create local_* peers. */
export function sanitizePeerBaseName(label: string, fallback: string): string {
  const s = label.replace(/[^\w\s\-·.@]/gu, "").trim();
  const base = (s || fallback).slice(0, 56).trim();
  return base || "midi";
}
