/**
 * Incremental MIDI 1.0 byte parser (running status, SysEx, realtime).
 */

export type MidiMonitorRowData = {
  atMs: number;
  label: string;
  channel: number | null;
  data: string;
  hex: string;
};

function hexOf(bytes: number[]): string {
  return bytes.map((b) => b.toString(16).padStart(2, "0")).join(" ");
}

const RT: Record<number, string> = {
  0xf8: "Clock",
  0xfa: "Start",
  0xfb: "Continue",
  0xfc: "Stop",
  0xfe: "ActiveSense",
  0xff: "Reset",
};

function voiceDetail(status: number, d1: number, d2: number | undefined): string {
  const hi = status & 0xf0;
  const ch = (status & 0x0f) + 1;
  switch (hi) {
    case 0x80:
      return `NoteOff ch${ch} note=${d1} vel=${d2 ?? 0}`;
    case 0x90:
      return `NoteOn ch${ch} note=${d1} vel=${d2 ?? 0}`;
    case 0xa0:
      return `PolyPressure ch${ch} note=${d1} val=${d2 ?? 0}`;
    case 0xb0:
      return `CC ch${ch} cc=${d1} val=${d2 ?? 0}`;
    case 0xc0:
      return `Program ch${ch} prog=${d1}`;
    case 0xd0:
      return `ChanPressure ch${ch} val=${d1}`;
    case 0xe0: {
      const v = d2 !== undefined ? d1 | (d2 << 7) : d1;
      return `PitchBend ch${ch} val=${v}`;
    }
    default:
      return `Voice ${status.toString(16)}`;
  }
}

export type MidiParseBuffer = {
  pending: number[];
  running: number | null;
  inSysex: boolean;
};

export function createMidiParseBuffer(): MidiParseBuffer {
  return { pending: [], running: null, inSysex: false };
}

function nowMs(): number {
  return typeof performance !== "undefined" ? performance.now() : Date.now();
}

function pad2(n: number): string {
  return String(n).padStart(2, "0");
}

function pad3(n: number): string {
  return String(n).padStart(3, "0");
}

/**
 * Monotonic clock (e.g. `performance.now()`): `MM:SS.mmm`, or `H:MM:SS.mmm` from 1h up.
 */
export function formatMonitorClockMs(totalMs: number): string {
  if (!Number.isFinite(totalMs) || totalMs < 0) return "—";
  const w = Math.floor(totalMs);
  const fracMs = w % 1000;
  const totalSec = Math.floor(w / 1000);
  const sec = totalSec % 60;
  const totalMin = Math.floor(totalSec / 60);
  const min = totalMin % 60;
  const hours = Math.floor(totalMin / 60);
  const tail = `${pad2(min)}:${pad2(sec)}.${pad3(fracMs)}`;
  if (hours > 0) return `${hours}:${tail}`;
  return tail;
}

/**
 * Append raw MIDI bytes and emit one row per complete message.
 */
export function feedMidiBytes(
  buf: MidiParseBuffer,
  chunk: Uint8Array,
  emit: (r: MidiMonitorRowData) => void,
): void {
  for (let i = 0; i < chunk.length; i++) buf.pending.push(chunk[i]!);

  while (buf.pending.length > 0) {
    if (buf.inSysex) {
      const end = buf.pending.indexOf(0xf7);
      if (end === -1) return;
      const msg = buf.pending.splice(0, end + 1);
      buf.inSysex = false;
      emit({
        atMs: nowMs(),
        label: "SysEx",
        channel: null,
        data: `${msg.length} bytes`,
        hex: hexOf(msg),
      });
      continue;
    }

    const b0 = buf.pending[0]!;

    if (b0 === 0xf0) {
      buf.inSysex = true;
      continue;
    }

    if (b0 >= 0xf8) {
      buf.pending.shift();
      emit({
        atMs: nowMs(),
        label: RT[b0] ?? `System ${b0.toString(16)}`,
        channel: null,
        data: "—",
        hex: b0.toString(16).padStart(2, "0"),
      });
      continue;
    }

    let status: number;
    if (b0 < 0x80) {
      if (buf.running === null) {
        buf.pending.shift();
        continue;
      }
      status = buf.running;
    } else {
      status = b0;
      const u = status & 0xf0;
      if (u >= 0x80 && u <= 0xe0) buf.running = status;
    }

    const u = status & 0xf0;
    let need = 0;
    if (u === 0xc0 || u === 0xd0) need = 1;
    else if (u >= 0x80 && u <= 0xe0) need = 2;
    else {
      buf.pending.shift();
      buf.running = null;
      continue;
    }

    const headerLen = b0 < 0x80 ? 0 : 1;
    if (buf.pending.length < headerLen + need) return;

    let rawForHex: number[];
    let d1: number;
    let d2: number | undefined;

    if (b0 < 0x80) {
      const slice = buf.pending.splice(0, need);
      d1 = slice[0]!;
      d2 = need > 1 ? slice[1] : undefined;
      rawForHex = [status, ...slice];
    } else {
      buf.pending.shift();
      const slice = buf.pending.splice(0, need);
      d1 = slice[0]!;
      d2 = need > 1 ? slice[1] : undefined;
      rawForHex = [status, ...slice];
    }

    const detail = voiceDetail(status, d1, d2);
    const ch = (status & 0x0f) + 1;

    emit({
      atMs: nowMs(),
      label: detail,
      channel: u >= 0x80 && u <= 0xe0 ? ch : null,
      data: detail,
      hex: hexOf(rawForHex),
    });
  }
}
