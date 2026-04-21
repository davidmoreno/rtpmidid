import { describe, expect, it } from "vitest";
import { normalizeRtpMidiUdpPort } from "./midiEnumerate";

describe("normalizeRtpMidiUdpPort", () => {
  it("defaults invalid or missing values to 5004", () => {
    expect(normalizeRtpMidiUdpPort(0)).toBe("5004");
    expect(normalizeRtpMidiUdpPort(-1)).toBe("5004");
    expect(normalizeRtpMidiUdpPort(NaN as unknown as number)).toBe("5004");
    expect(normalizeRtpMidiUdpPort("")).toBe("5004");
    expect(normalizeRtpMidiUdpPort(70000)).toBe("5004");
  });

  it("accepts valid UDP port numbers", () => {
    expect(normalizeRtpMidiUdpPort(5004)).toBe("5004");
    expect(normalizeRtpMidiUdpPort("5004")).toBe("5004");
    expect(normalizeRtpMidiUdpPort(6000)).toBe("6000");
  });
});
