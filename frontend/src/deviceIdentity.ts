/** key=value device identity grammar (mirrors src/device_identity.cpp). */

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
];

export function deviceTypeDef(typePrefix: string): DeviceTypeDef | undefined {
  return DEVICE_TYPE_DEFS.find((d) => d.typePrefix === typePrefix);
}

const TYPE_LABELS: Record<string, string> = {
  alsa_seq: "ALSA",
  rawmidi: "Raw MIDI",
  rtpmidi_client: "RTP client",
  rtpmidi_server: "RTP server",
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

function activeField(parsed: ParsedIdentity, key: string): string | undefined {
  return parsed.fields.find((f) => f.key === key && !f.bracketed)?.value;
}

/**
 * Map a registry identity to an endpoint id understood by endpoint.connect /
 * monitor.start. The daemon materializes peers on demand (e.g. RTP client).
 */
export function endpointIdFromIdentity(identity: string): string | null {
  const parsed = parseIdentity(identity);
  if (!parsed) return null;
  switch (parsed.typePrefix) {
    case "rtpmidi_client": {
      const hostname = activeField(parsed, "hostname");
      if (!hostname) return null;
      const port = activeField(parsed, "port") ?? "5004";
      return `host:${hostname}:${port}`;
    }
    case "rawmidi": {
      const device = activeField(parsed, "device");
      return device ? `raw:${device}` : null;
    }
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
