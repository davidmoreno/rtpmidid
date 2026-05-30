import { describe, expect, it } from "vitest";
import {
  formatIdentityLabel,
  identityFromAlsaNames,
  identityFromMdnsGroup,
  identityFromPeerRow,
  identityFromRtpClientConnect,
  parseIdentity,
  serializeIdentity,
} from "./deviceIdentity";
import type { MdnsRemoteGroup, RouterPeer } from "./model";

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

  it("builds rtpmidi_client identity from hostname/service/port", () => {
    expect(
      identityFromRtpClientConnect("192.168.1.80", "5004", "DeepMind 12D"),
    ).toBe("rtpmidi_client:hostname=192.168.1.80,port=5004,service=DeepMind 12D");
    expect(
      identityFromRtpClientConnect("192.168.1.80", "4001", "DeepMind 12D"),
    ).toBe("rtpmidi_client:hostname=192.168.1.80,port=4001,service=DeepMind 12D");
  });

  it("builds alsa_seq identity from client/port names", () => {
    expect(identityFromAlsaNames("A", "P1")).toBe("alsa_seq:client=A,port=P1");
  });

  it("builds rtpmidi_client identity from mDNS group", () => {
    const g: MdnsRemoteGroup = {
      name: "Synth",
      port: 5004,
      addresses: ["host.local"],
      ips: ["192.168.1.5"],
      instances: [
        {
          name: "Synth",
          hostname: "host.local",
          ip: "192.168.1.5",
          port: 5004,
        },
      ],
    };
    expect(identityFromMdnsGroup(g)).toBe(
      "rtpmidi_client:hostname=host.local,port=5004,service=Synth",
    );
  });

  it("derives identity from rtp client peer row", () => {
    const peer: RouterPeer = {
      id: 3,
      name: "WEB · host",
      type: "peer_device_rtpmidi_client_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {
        connect_hostname: "192.168.1.80",
        connect_port: "5004",
        peer: { remote: { name: "DeepMind 12D", hostname: "192.168.1.80" } },
      },
    };
    expect(identityFromPeerRow(peer)).toBe(
      "rtpmidi_client:hostname=192.168.1.80,port=5004,service=DeepMind 12D",
    );
  });
});
