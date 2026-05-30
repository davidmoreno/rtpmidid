import { describe, expect, it } from "vitest";
import { buildEndpoints, buildPickerEndpoints } from "./endpoints";
import type { RegistryDevice } from "./devicesList";

describe("buildPickerEndpoints", () => {
  it("includes offline registry rtpmidi_client not in live endpoints", () => {
    const registry: RegistryDevice = {
      identity: "rtpmidi_client:hostname=192.168.1.50,port=5004,service=My Synth",
      type: "rtpmidi_client",
      name: "My Synth",
      source: "manual",
      firstSeen: 1,
      lastSeen: 2,
      online: false,
    };

    const live = buildEndpoints({
      alsaSeq: [],
      rawmidi: [],
      mdnsRemotes: [],
      peers: [],
    });
    expect(live.some((e) => e.identity.includes("192.168.1.50"))).toBe(false);

    const picker = buildPickerEndpoints({
      alsaSeq: [],
      rawmidi: [],
      mdnsRemotes: [],
      peers: [],
      registryDevices: [registry],
    });

    const hit = picker.find((e) =>
      e.identity.includes("hostname=192.168.1.50"),
    );
    expect(hit).toBeDefined();
    expect(hit!.kind).toBe("rtpmidi");
    expect(hit!.sub).toContain("creates peer on connect");
    expect(hit!.peerId).toBeUndefined();
  });

  it("does not duplicate an mDNS row already in buildEndpoints", () => {
    const mdnsRemotes = [
      {
        name: "Synth",
        hostname: "pi.local",
        ip: "192.168.1.10",
        port: 5004,
      },
    ];
    const registry: RegistryDevice = {
      identity:
        "rtpmidi_client:hostname=pi.local,port=5004,service=Synth",
      type: "rtpmidi_client",
      name: "Synth",
      source: "discovered",
      firstSeen: 1,
      lastSeen: 2,
      online: false,
    };

    const picker = buildPickerEndpoints({
      alsaSeq: [],
      rawmidi: [],
      mdnsRemotes,
      peers: [],
      registryDevices: [registry],
    });

    const rtp = picker.filter((e) => e.kind === "rtpmidi");
    expect(rtp).toHaveLength(1);
  });
});
