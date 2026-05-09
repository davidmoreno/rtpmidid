import { describe, expect, it } from "vitest";
import {
  createMidiParseBuffer,
  feedMidiBytes,
  formatMonitorClockMs,
  type MidiMonitorRowData,
} from "./midiMonitorParse";

describe("formatMonitorClockMs", () => {
  it("formats MM:SS.mmm under one hour", () => {
    expect(formatMonitorClockMs(0)).toBe("00:00.000");
    expect(formatMonitorClockMs(45)).toBe("00:00.045");
    expect(formatMonitorClockMs(999)).toBe("00:00.999");
    expect(formatMonitorClockMs(1000)).toBe("00:01.000");
    expect(formatMonitorClockMs(1234)).toBe("00:01.234");
    expect(formatMonitorClockMs(62003)).toBe("01:02.003");
    expect(formatMonitorClockMs(61234)).toBe("01:01.234");
    expect(formatMonitorClockMs(3599999)).toBe("59:59.999");
  });

  it("adds hours only from 1h upward", () => {
    expect(formatMonitorClockMs(3600000)).toBe("1:00:00.000");
    expect(formatMonitorClockMs(3600000 + 62003)).toBe("1:01:02.003");
    expect(formatMonitorClockMs(3723000)).toBe("1:02:03.000");
  });

  it("floors sub-millisecond values to ms field", () => {
    expect(formatMonitorClockMs(1000.9)).toBe("00:01.000");
  });
});

describe("feedMidiBytes", () => {
  it("parses NoteOn", () => {
    const buf = createMidiParseBuffer();
    const rows: MidiMonitorRowData[] = [];
    feedMidiBytes(buf, new Uint8Array([0x90, 60, 127]), (r) => rows.push(r));
    expect(rows.length).toBe(1);
    expect(rows[0]?.label).toContain("NoteOn");
    expect(rows[0]?.label).toContain("ch1");
  });

  it("uses running status", () => {
    const buf = createMidiParseBuffer();
    const rows: MidiMonitorRowData[] = [];
    feedMidiBytes(buf, new Uint8Array([0x90, 60, 127, 61, 100]), (r) =>
      rows.push(r),
    );
    expect(rows.length).toBe(2);
    expect(rows[1]?.label).toContain("NoteOn");
  });

  it("parses CC", () => {
    const buf = createMidiParseBuffer();
    const rows: MidiMonitorRowData[] = [];
    feedMidiBytes(buf, new Uint8Array([0xb0, 7, 100]), (r) => rows.push(r));
    expect(rows.length).toBe(1);
    expect(rows[0]?.label).toContain("CC");
    expect(rows[0]?.label).toContain("cc=7");
  });

  it("parses SysEx until F7", () => {
    const buf = createMidiParseBuffer();
    const rows: MidiMonitorRowData[] = [];
    feedMidiBytes(
      buf,
      new Uint8Array([0xf0, 0x01, 0x02, 0xf7]),
      (r) => rows.push(r),
    );
    expect(rows.length).toBe(1);
    expect(rows[0]?.label).toBe("SysEx");
    expect(rows[0]?.hex).toContain("f0");
    expect(rows[0]?.hex).toContain("f7");
  });
});
