/** Responses from daemon `midi.listAlsaSeq` / `midi.listRawMidi`. */

export type MidiAlsaSeqEntry = {
  type: "alsa_seq";
  id: string;
  client: number;
  port: number;
  client_name: string;
  port_name: string;
  label: string;
  kind: string;
};

export type MidiRawmidiEntry = {
  type: "rawmidi";
  id: string;
  device: string;
  label: string;
  kind: string;
};

/** Pick for mDNS Connect: create `local_alsa_peer_t` or `local_rawmidi_t` then wire RTP client. */
export type WireLocalChoice =
  | {
      mode: "alsa_seq";
      listingLabel: string;
      /** Enumerated ALSA source; daemon subscribes this client:port → new rtpmidid port. */
      client: number;
      port: number;
    }
  | { mode: "rawmidi"; device: string; listingLabel: string };

/** RTP-MIDI / mDNS UDP port string for `network_rtpmidi_client_t` (default Apple 5004). */
export function normalizeRtpMidiUdpPort(p: number | string): string {
  const n = typeof p === "number" ? p : Number(String(p).trim());
  if (!Number.isFinite(n) || n < 1 || n > 65535) return "5004";
  return String(Math.floor(n));
}

export function parseMidiAlsaSeqResult(r: unknown): MidiAlsaSeqEntry[] | null {
  if (r && typeof r === "object" && "error" in r) return null;
  if (!Array.isArray(r)) return null;
  const out: MidiAlsaSeqEntry[] = [];
  for (const row of r) {
    if (!row || typeof row !== "object") continue;
    const o = row as Record<string, unknown>;
    if (o.type !== "alsa_seq") continue;
    out.push({
      type: "alsa_seq",
      id: String(o.id ?? ""),
      client: Number(o.client),
      port: Number(o.port),
      client_name: String(o.client_name ?? ""),
      port_name: String(o.port_name ?? ""),
      label: String(o.label ?? o.id ?? ""),
      kind: String(o.kind ?? ""),
    });
  }
  return out;
}

export function parseMidiRawmidiResult(r: unknown): MidiRawmidiEntry[] | null {
  if (r && typeof r === "object" && "error" in r) return null;
  if (!Array.isArray(r)) return null;
  const out: MidiRawmidiEntry[] = [];
  for (const row of r) {
    if (!row || typeof row !== "object") continue;
    const o = row as Record<string, unknown>;
    if (o.type !== "rawmidi") continue;
    out.push({
      type: "rawmidi",
      id: String(o.id ?? ""),
      device: String(o.device ?? ""),
      label: String(o.label ?? o.device ?? ""),
      kind: String(o.kind ?? "rawmidi"),
    });
  }
  return out;
}

/** Safe peer name for router.create local_* peers. */
export function sanitizePeerBaseName(label: string, fallback: string): string {
  const s = label.replace(/[^\w\s\-·.@]/gu, "").trim();
  const base = (s || fallback).slice(0, 56).trim();
  return base || "midi";
}
