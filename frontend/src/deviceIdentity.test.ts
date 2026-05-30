import { describe, expect, it } from "vitest";
import {
  endpointIdFromIdentity,
  formatIdentityLabel,
  parseIdentity,
  serializeIdentity,
} from "./deviceIdentity";

describe("deviceIdentity", () => {
  it("round-trips a simple identity", () => {
    const raw = "alsa_seq:client=Peak,port=In";
    const parsed = parseIdentity(raw);
    expect(parsed).not.toBeNull();
    expect(serializeIdentity(parsed!)).toBe(raw);
  });

  it("preserves bracketed fields", () => {
    const raw = "rtpmidi_server:name=Peak,[port=5004]";
    const parsed = parseIdentity(raw);
    expect(parsed?.fields.find((f) => f.key === "port")?.bracketed).toBe(true);
    expect(serializeIdentity(parsed!)).toBe(raw);
  });

  it("formats human labels", () => {
    expect(formatIdentityLabel("alsa_seq:client=Peak,port=In")).toContain("Peak");
  });

  it("maps rtpmidi_client identity to host endpoint id", () => {
    expect(
      endpointIdFromIdentity(
        "rtpmidi_client:hostname=192.168.1.80,service=DeepMind 12D",
      ),
    ).toBe("host:192.168.1.80:5004");
    expect(
      endpointIdFromIdentity(
        "rtpmidi_client:hostname=192.168.1.80,service=DeepMind 12D,port=4001",
      ),
    ).toBe("host:192.168.1.80:4001");
  });
});
