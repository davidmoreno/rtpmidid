/** key=value device identity grammar (mirrors src/device_identity.cpp). */

import type { MidiAlsaSeqEntry, MidiRawmidiEntry } from "./midiEnumerate";
import type { MdnsRemoteGroup, RouterPeer } from "./model";

export type IdentityField = {
  key: string;
  value: string;
  /** Bracketed fields are remembered but not used for matching. */
  bracketed: boolean;
};

export type ParsedIdentity = {
  typePrefix: string;
  fields: IdentityField[];
};

const ESCAPE_CHARS = new Set(["\\", ":", ",", "=", "[", "]"]);

function escapeValue(s: string): string {
  let out = "";
  for (const c of s) {
    if (ESCAPE_CHARS.has(c)) out += "\\" + c;
    else out += c;
  }
  return out;
}

function readUnescapedUntil(text: string, start: number, stop: string): [string, number] | null {
  let out = "";
  let i = start;
  while (i < text.length) {
    const c = text[i];
    if (c === "\\") {
      if (i + 1 >= text.length) return null;
      out += text[i + 1];
      i += 2;
      continue;
    }
    if (c === stop) return [out, i];
    out += c;
    i += 1;
  }
  if (stop === ",") return [out, i];
  return null;
}

export function parseIdentity(text: string): ParsedIdentity | null {
  const trimmed = text.trim();
  const colon = trimmed.indexOf(":");
  if (colon <= 0 || colon + 1 >= trimmed.length) return null;
  const typePrefix = trimmed.slice(0, colon);
  const fieldsText = trimmed.slice(colon + 1);
  if (!fieldsText) return null;

  const fields: IdentityField[] = [];
  const seen = new Set<string>();
  let i = 0;
  while (i < fieldsText.length) {
    let bracketed = false;
    if (fieldsText[i] === "[") {
      bracketed = true;
      i += 1;
    }
    const keyRead = readUnescapedUntil(fieldsText, i, "=");
    if (!keyRead || !keyRead[0]) return null;
    const [key, afterKey] = keyRead;
    if (fieldsText[afterKey] !== "=") return null;
    i = afterKey + 1;
    const valueStop = bracketed ? "]" : ",";
    const valRead = readUnescapedUntil(fieldsText, i, valueStop);
    if (!valRead || !valRead[0]) return null;
    const [value, afterVal] = valRead;
    i = afterVal;
    if (bracketed) {
      if (fieldsText[i] !== "]") return null;
      i += 1;
    }
    if (seen.has(key)) return null;
    seen.add(key);
    fields.push({ key, value, bracketed });
    if (i >= fieldsText.length) break;
    if (fieldsText[i] !== ",") return null;
    i += 1;
  }
  fields.sort((a, b) => a.key.localeCompare(b.key));
  return { typePrefix, fields };
}

/** Canonical form for map keys and equality (field order normalized). */
export function canonicalIdentity(identity: string): string {
  const parsed = parseIdentity(identity);
  return parsed ? serializeIdentity(parsed) : identity.trim();
}

export function identitiesEqual(a: string, b: string): boolean {
  return canonicalIdentity(a) === canonicalIdentity(b);
}

export function serializeIdentity(parsed: ParsedIdentity): string {
  const sorted = [...parsed.fields].sort((a, b) => a.key.localeCompare(b.key));
  const parts = sorted.map((f) => {
    const prefix = f.bracketed ? "[" : "";
    const suffix = f.bracketed ? "]" : "";
    return `${prefix}${f.key}=${escapeValue(f.value)}${suffix}`;
  });
  return `${parsed.typePrefix}:${parts.join(",")}`;
}

export type DeviceTypeDef = {
  typePrefix: string;
  label: string;
  fields: { key: string; label: string; placeholder?: string; optional?: boolean }[];
};

export const DEVICE_TYPE_DEFS: DeviceTypeDef[] = [
  {
    typePrefix: "alsa_seq",
    label: "ALSA sequencer port",
    fields: [
      { key: "client", label: "Client name", placeholder: "Peak" },
      { key: "port", label: "Port name", placeholder: "In" },
      { key: "name", label: "Display name (fallback)", placeholder: "Network Export", optional: true },
    ],
  },
  {
    typePrefix: "rawmidi",
    label: "Raw MIDI device",
    fields: [
      { key: "device", label: "Device path", placeholder: "/dev/snd/midiC0D0" },
      { key: "name", label: "Display name", placeholder: "MIDI Export", optional: true },
    ],
  },
  {
    typePrefix: "rtpmidi_client",
    label: "RTP-MIDI client (remote)",
    fields: [
      { key: "hostname", label: "Hostname", placeholder: "host.local" },
      { key: "service", label: "Service name", placeholder: "Peak Out" },
      { key: "port", label: "UDP port", placeholder: "5004", optional: true },
    ],
  },
  {
    typePrefix: "rtpmidi_server",
    label: "RTP-MIDI server (local announce)",
    fields: [
      { key: "name", label: "Service name", placeholder: "Peak InOut" },
      { key: "port", label: "UDP port", placeholder: "5004", optional: true },
    ],
  },
  {
    typePrefix: "rtpmidi_session",
    label: "RTP-MIDI session (incoming)",
    fields: [
      { key: "hostname", label: "Hostname", placeholder: "host.local" },
      { key: "service", label: "Service name", placeholder: "Peak In" },
      { key: "name", label: "Display name (fallback)", optional: true },
    ],
  },
  {
    typePrefix: "rtpmidi_multi",
    label: "RTP-MIDI multi-listener",
    fields: [
      { key: "name", label: "Service name", placeholder: "Network MIDI" },
      { key: "port", label: "UDP port", placeholder: "5004", optional: true },
    ],
  },
  {
    typePrefix: "alsa_listener",
    label: "ALSA RTP listener",
    fields: [
      { key: "service", label: "Remote service name", placeholder: "Synth Out" },
      { key: "name", label: "Display name (fallback)", optional: true },
    ],
  },
  {
    typePrefix: "alsa_multi",
    label: "ALSA network export",
    fields: [{ key: "name", label: "Service name", placeholder: "Network Export" }],
  },
];

export function deviceTypeDef(typePrefix: string): DeviceTypeDef | undefined {
  return DEVICE_TYPE_DEFS.find((d) => d.typePrefix === typePrefix);
}

const TYPE_LABELS: Record<string, string> = {
  alsa_seq: "ALSA",
  rawmidi: "Raw MIDI",
  rtpmidi_client: "RTP client",
  rtpmidi_server: "RTP server",
  rtpmidi_session: "RTP session",
  alsa_multi: "ALSA export",
  rtpmidi_multi: "RTP multi",
  alsa_listener: "ALSA listener",
};

export function formatIdentityLabel(identity: string): string {
  const parsed = parseIdentity(identity);
  if (!parsed) return identity;
  const prefix = TYPE_LABELS[parsed.typePrefix] ?? parsed.typePrefix;
  const active = parsed.fields.filter((f) => !f.bracketed);
  const parts = active.map((f) => {
    if (f.key === "client" && parsed.fields.some((x) => x.key === "port" && !x.bracketed)) {
      const port = parsed.fields.find((x) => x.key === "port");
      if (port && !port.bracketed) return `${f.value} · ${port.value}`;
    }
    if (f.key === "port" && active.some((x) => x.key === "client")) return null;
    return f.value || f.key;
  }).filter((x): x is string => !!x);
  return parts.length ? `${prefix}: ${parts.join(" · ")}` : prefix;
}

export function fieldLabel(key: string): string {
  for (const def of DEVICE_TYPE_DEFS) {
    const f = def.fields.find((x) => x.key === key);
    if (f) return f.label;
  }
  return key;
}

export function identityFromForm(
  typePrefix: string,
  values: Record<string, string>,
): ParsedIdentity | null {
  const def = deviceTypeDef(typePrefix);
  if (!def) return null;
  const fields: IdentityField[] = [];
  for (const spec of def.fields) {
    const v = (values[spec.key] ?? "").trim();
    if (!v && !spec.optional) return null;
    if (v) fields.push({ key: spec.key, value: v, bracketed: false });
  }
  if (!fields.length) return null;
  return { typePrefix, fields };
}

export function identityToFormValues(
  identity: string,
): { typePrefix: string; values: Record<string, string> } | null {
  const parsed = parseIdentity(identity);
  if (!parsed) return null;
  const values: Record<string, string> = {};
  for (const f of parsed.fields) {
    if (!f.bracketed) values[f.key] = f.value;
  }
  return { typePrefix: parsed.typePrefix, values };
}

function makeIdentity(
  typePrefix: string,
  fields: IdentityField[],
): string | null {
  if (!fields.length) return null;
  return serializeIdentity({ typePrefix, fields });
}

function field(key: string, value: string): IdentityField {
  return { key, value, bracketed: false };
}

function realHost(h: string): string {
  const t = h.trim();
  return t && t !== "null" ? t : "";
}

/** Resolve an ALSA numeric address to a device identity (names from subs or enumerate). */
export function identityFromAlsaAddress(
  client: number,
  port: number,
  clientName: string | undefined,
  portName: string | undefined,
  alsaSeq: MidiAlsaSeqEntry[],
): string {
  const row = alsaSeq.find((a) => a.client === client && a.port === port);
  const cn = (clientName ?? row?.client_name ?? "").trim();
  const pn = (portName ?? row?.port_name ?? "").trim();
  return (
    identityFromAlsaNames(cn, pn) ??
    `alsa_seq:client=${cn || String(client)},port=${pn || String(port)}`
  );
}

export function identityFromAlsaNames(
  clientName: string,
  portName: string,
): string | null {
  const cn = clientName.trim();
  const pn = portName.trim();
  if (!cn || !pn) return null;
  return makeIdentity("alsa_seq", [field("client", cn), field("port", pn)]);
}

export function identityFromAlsaEntry(e: MidiAlsaSeqEntry): string | null {
  if (e.client_name && e.port_name) {
    return identityFromAlsaNames(e.client_name, e.port_name);
  }
  const label = (e.label || "").trim();
  if (label) return makeIdentity("alsa_seq", [field("name", label)]);
  return null;
}

export function identityFromRawEntry(e: MidiRawmidiEntry): string | null {
  const dev = (e.device || "").trim();
  if (!dev) return null;
  const fields: IdentityField[] = [field("device", dev)];
  const name = (e.label || "").trim();
  if (name) fields.push(field("name", name));
  return makeIdentity("rawmidi", fields);
}

export function identityFromMdnsGroup(g: MdnsRemoteGroup): string | null {
  const service = (g.name || "").trim();
  if (!service) return null;
  const hostnames = g.instances.map((r) => r.hostname.trim()).filter((x) => x);
  const ips = g.instances.map((r) => r.ip.trim()).filter((x) => x);
  const hostname = hostnames[0] || ips[0] || "";
  if (!hostname) return null;
  const fields: IdentityField[] = [
    field("hostname", hostname),
    field("service", service),
  ];
  const portStr = String(g.port).trim();
  if (portStr) fields.push(field("port", portStr));
  return makeIdentity("rtpmidi_client", fields);
}

export function identityFromRtpClientConnect(
  hostname: string,
  port: string,
  service: string,
): string | null {
  const h = realHost(hostname);
  const svc = service.trim();
  if (!h || !svc) return null;
  const fields: IdentityField[] = [field("hostname", h), field("service", svc)];
  const p = port.trim();
  if (p) fields.push(field("port", p));
  return makeIdentity("rtpmidi_client", fields);
}

/** Mirrors `compute_device_identity` in src/device_identity_from_peer.cpp. */
export function identityFromPeerRow(peer: RouterPeer): string | null {
  const raw = peer.raw as Record<string, unknown>;
  const peerName = (peer.name || String(raw.name ?? "")).trim();

  switch (peer.type) {
    case "peer_device_alsa_seq_t": {
      const asf = raw.alsa_subscribe_from as
        | { client_name?: string; port_name?: string }
        | undefined;
      if (asf?.client_name && asf?.port_name) {
        return identityFromAlsaNames(asf.client_name, asf.port_name);
      }
      if (peerName) return makeIdentity("alsa_seq", [field("name", peerName)]);
      return null;
    }
    case "peer_device_rawmidi_t": {
      const fields: IdentityField[] = [];
      const dev = String(raw.device ?? "").trim();
      if (dev) fields.push(field("device", dev));
      if (peerName) fields.push(field("name", peerName));
      return makeIdentity("rawmidi", fields);
    }
    case "peer_device_rtpmidi_client_t": {
      let hostname = realHost(String(raw.connect_hostname ?? "").trim());
      if (!hostname) {
        const p = raw.peer as Record<string, unknown> | undefined;
        const rem = p?.remote as Record<string, unknown> | undefined;
        if (rem) hostname = realHost(String(rem.hostname ?? "").trim());
      }
      let service = "";
      const p = raw.peer as Record<string, unknown> | undefined;
      const rem = p?.remote as Record<string, unknown> | undefined;
      if (rem) service = String(rem.name ?? "").trim();
      if (!service) service = peerName;
      const port = String(raw.connect_port ?? raw.port ?? "").trim();
      return identityFromRtpClientConnect(hostname, port, service);
    }
    case "peer_device_rtpmidi_session_t": {
      const p = raw.peer as Record<string, unknown> | undefined;
      const rem = p?.remote as Record<string, unknown> | undefined;
      if (rem) {
        const h = realHost(String(rem.hostname ?? "").trim());
        const n = String(rem.name ?? "").trim();
        if (h && n) {
          return makeIdentity("rtpmidi_session", [field("hostname", h), field("service", n)]);
        }
      }
      if (peerName) return makeIdentity("rtpmidi_session", [field("name", peerName)]);
      return null;
    }
    case "peer_export_rtpmidi_server_t": {
      if (!peerName) return null;
      const fields: IdentityField[] = [field("name", peerName)];
      if (raw.port !== undefined && raw.port !== null && String(raw.port) !== "") {
        fields.push(field("port", String(raw.port)));
      }
      return makeIdentity("rtpmidi_server", fields);
    }
    case "peer_import_rtpmidi_t": {
      const listening = raw.listening as { name?: string; midi_port?: unknown } | undefined;
      let name =
        listening?.name && String(listening.name).length > 0
          ? String(listening.name)
          : peerName;
      if (!name) return null;
      const fields: IdentityField[] = [field("name", name)];
      if (listening?.midi_port !== undefined && listening.midi_port !== null) {
        fields.push(field("port", String(listening.midi_port)));
      }
      return makeIdentity("rtpmidi_multi", fields);
    }
    case "peer_import_alsa_rtp_t": {
      if (!peerName) return null;
      const pos = peerName.indexOf(" <-> ");
      if (pos >= 0) {
        const remote = peerName.substring(pos + 5).trim();
        if (remote) return makeIdentity("alsa_listener", [field("service", remote)]);
      }
      return makeIdentity("alsa_listener", [field("name", peerName)]);
    }
    case "peer_export_alsa_network_t": {
      if (!peerName) return null;
      return makeIdentity("alsa_multi", [field("name", peerName)]);
    }
    case "webui_midi_monitor_peer_t":
      return null;
    default:
      return null;
  }
}

export type ConnectionDirection = "a2b" | "b2a" | "both";

export function directionArrow(dir: ConnectionDirection): string {
  switch (dir) {
    case "a2b":
      return "→";
    case "b2a":
      return "←";
    case "both":
      return "↔";
  }
}

export function directionLabel(dir: ConnectionDirection, sideA: string, sideB: string): string {
  const a = formatIdentityLabel(sideA);
  const b = formatIdentityLabel(sideB);
  switch (dir) {
    case "a2b":
      return `${a} → ${b}`;
    case "b2a":
      return `${a} ← ${b}`;
    case "both":
      return `${a} ↔ ${b}`;
  }
}
