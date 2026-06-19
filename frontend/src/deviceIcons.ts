/**
 * Device brand icon matching.
 *
 * Uses Parcel's `new URL(…, import.meta.url)` for asset resolution —
 * Parcel statically analyzes this and returns the hashed URL at build time.
 *
 * Only uses REAL official brand logos. Unknown brands fall through to:
 * ALSA / Linux Tux / generic MIDI DIN connector icon.
 */

export type BrandMatch = {
  id: string;
  label: string;
  /** Resolved URL (data URI or external hashed file, per Parcel). */
  url: string;
};

/**
 * Resolve an asset URL via Parcel.  The `new URL(…, import.meta.url)` pattern
 * is statically analyzed by Parcel 2 — it returns the hashed output URL.
 */
const icons = {
  roland: new URL("./assets/brand-icons/roland.svg", import.meta.url).href,
  yamaha: new URL("./assets/brand-icons/yamaha.svg", import.meta.url).href,
  korg: new URL("./assets/brand-icons/korg.svg", import.meta.url).href,
  moog: new URL("./assets/brand-icons/moog.svg", import.meta.url).href,
  akai: new URL("./assets/brand-icons/akai.svg", import.meta.url).href,
  behringer: new URL("./assets/brand-icons/behringer.svg", import.meta.url).href,
  native_instruments: new URL("./assets/brand-icons/native_instruments.svg", import.meta.url).href,
  elektron: new URL("./assets/brand-icons/elektron.svg", import.meta.url).href,
  nord: new URL("./assets/brand-icons/nord.svg", import.meta.url).href,
  dave_smith: new URL("./assets/brand-icons/dave_smith.svg", import.meta.url).href,
  mackie: new URL("./assets/brand-icons/mackie.svg", import.meta.url).href,
  ableton: new URL("./assets/brand-icons/ableton.png", import.meta.url).href,
  casio: new URL("./assets/brand-icons/casio.png", import.meta.url).href,
  alsa: new URL("./assets/brand-icons/alsa.svg", import.meta.url).href,
  linux: new URL("./assets/brand-icons/linux.svg", import.meta.url).href,
  midi: new URL("./assets/brand-icons/midi.svg", import.meta.url).href,
};

export { icons };

/**
 * Regex → brand id mapping.
 * Only real logos we have downloaded. Everything else → MIDI connector fallback.
 */
const BRAND_PATTERNS: { re: RegExp; id: keyof typeof icons; label: string }[] = [
  // ─── Roland ───
  { re: /\bRoland\b/i, id: "roland", label: "Roland" },
  { re: /\b(Jupiter|Juno|TR-8|SH-|JD-X|FA-|MC-|SP-|D-50|JX-|MKS|JV|XV|Integra|Aira|TD-|V-Synth|Fantom)\b/i, id: "roland", label: "Roland" },

  // ─── Yamaha ───
  { re: /\bYamaha\b/i, id: "yamaha", label: "Yamaha" },
  { re: /\b(DX7|DX[1579]|Motif|Montage|MODX|CP[^U]|PSR-|Tyros|Genos|Reface|CK[^ ])\b/i, id: "yamaha", label: "Yamaha" },

  // ─── Korg ───
  { re: /\bKorg\b/i, id: "korg", label: "Korg" },
  { re: /\b(Minilogue|Monologue|Prologue|Wavestate|Opsix|Modwave|MS-20|Volca|Electribe|Kronos|Nautilus|Karma|M1|Triton|microKORG|minilogue)\b/i, id: "korg", label: "Korg" },

  // ─── Moog ───
  { re: /\b(Moog|Minimoog|Sub\s?37|Mother-32|DFAM|Grandmother|Matriarch|Minitaur|Theremin|Model\s?D|Subsequent|Voyager)\b/i, id: "moog", label: "Moog" },

  // ─── Akai ───
  { re: /\b(Akai|MPC\s?Live|MPC\s?X|MPC\s?One|MPC\s?Key|APC|MPK)\b/i, id: "akai", label: "Akai" },
  { re: /\bMPC\b/i, id: "akai", label: "Akai" },

  // ─── Behringer ───
  { re: /\bBehringer\b/i, id: "behringer", label: "Behringer" },

  // ─── Native Instruments ───
  { re: /\b(Native\s?Instruments|Maschine|Komplete|Traktor|Reaktor|Kontakt|FM8|Massive)\b/i, id: "native_instruments", label: "NI" },

  // ─── Elektron ───
  { re: /\b(Elektron|Digitakt|Digitone|Octatrack|Analog\s?(Four|Keys|Heat|Rytm)|Syntakt|Model:?[CS]|Rytm|Machinedrum)\b/i, id: "elektron", label: "Elektron" },

  // ─── Nord (Clavia) ───
  { re: /\bNord\b/i, id: "nord", label: "Nord" },
  { re: /\bClavia\b/i, id: "nord", label: "Nord" },

  // ─── Dave Smith Instruments / Sequential ───
  { re: /\b(Dave\s?Smith|DSI|Prophet|Sequential)\b/i, id: "dave_smith", label: "DSI" },

  // ─── Ableton ───
  { re: /\b(Ableton|Push)\b/i, id: "ableton", label: "Ableton" },

  // ─── Casio ───
  { re: /\b(Casio|Casiotone|CZ-|SK-|CT-)\b/i, id: "casio", label: "Casio" },

  // ─── Mackie ───
  { re: /\bMackie\b/i, id: "mackie", label: "Mackie" },
];

/**
 * Match a device name/label/sub to a brand icon URL.
 * Falls through: brand patterns → type fallbacks → null (generic MIDI icon).
 */
export function matchBrandIcon(
  label: string,
  sub?: string,
  identity?: string,
): BrandMatch | null {
  const candidates = [label, sub ?? "", identity ?? ""].filter(Boolean);

  for (const text of candidates) {
    const m = matchBrand(text);
    if (m) {
      const url = icons[m.id];
      if (url) return { ...m, url };
    }
  }

  if (identity) {
    for (const val of parseIdentityFields(identity)) {
      const m = matchBrand(val);
      if (m) {
        const url = icons[m.id];
        if (url) return { ...m, url };
      }
    }
  }

  return matchTypeFallback(label, sub ?? "", identity ?? "");
}

function matchBrand(text: string): { id: keyof typeof icons; label: string } | null {
  if (!text) return null;
  for (const { re, id, label } of BRAND_PATTERNS) {
    if (re.test(text)) return { id, label };
  }
  return null;
}

function matchTypeFallback(
  label: string,
  sub: string,
  identity: string,
): BrandMatch | null {
  const id = identity || "";
  const l = label || "";
  const s = sub || "";

  if (id.startsWith("alsa_seq:") || /\bALSA\b/i.test(l) || s.includes("alsa_seq")) {
    const url = icons.alsa;
    if (url) return { id: "alsa", label: "ALSA", url };
  }

  if (
    /virmidi|Virtual.?Raw.?MIDI|Midi.?Through|Virtual.?MIDI/i.test(l) ||
    /virmidi/i.test(id)
  ) {
    const url = icons.linux;
    if (url) return { id: "linux", label: "Linux", url };
  }

  if (id.startsWith("rawmidi:") || s.includes("rawmidi")) {
    const url = icons.midi;
    if (url) return { id: "midi", label: "MIDI", url };
  }

  if (/midi|port|usb|audio|interface|synth|sequencer|keyboard|controller/i.test(l)) {
    const url = icons.midi;
    if (url) return { id: "midi", label: "MIDI", url };
  }

  return null;
}

function parseIdentityFields(identity: string): string[] {
  const colon = identity.indexOf(":");
  if (colon < 0) return [];
  const body = identity.slice(colon + 1);
  const values: string[] = [];
  let i = 0;
  while (i < body.length) {
    const eq = body.indexOf("=", i);
    if (eq < 0) break;
    const valStart = eq + 1;
    let valEnd = body.length;
    let depth = 0;
    for (let j = valStart; j < body.length; j++) {
      if (body[j] === "\\") { j++; continue; }
      if (body[j] === "[" || body[j] === "{") depth++;
      if (body[j] === "]" || body[j] === "}") depth--;
      if (depth === 0 && body[j] === ",") { valEnd = j; break; }
    }
    let val = body.slice(valStart, valEnd);
    val = val.replace(/\\(.)/g, "$1");
    if (val) values.push(val);
    i = valEnd + 1;
  }
  return values;
}
