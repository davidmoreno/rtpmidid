import { describe, expect, it } from "vitest";
import { buildEndpoints, endpointIdForHost, type Endpoint } from "./endpoints";
import { mergeDeviceList, registryCardId } from "./mergeDeviceList";
import type { RegistryDevice } from "./devicesList";
import type { RouterPeer } from "./model";

describe("buildEndpoints host rtp clients", () => {
  it("adds host endpoint for direct rtp client peers", () => {
    const peers: RouterPeer[] = [
      {
        id: 7,
        name: "WEB · 192.168.1.80",
        type: "peer_device_rtpmidi_client_t",
        send_to: [3],
        recv: 12,
        sent: 4,
        raw: {
          connect_hostname: "192.168.1.80",
          connect_port: "5004",
          peer: { remote: { name: "DeepMind 12D", hostname: "192.168.1.80" } },
        },
      },
    ];
    const endpoints = buildEndpoints({
      alsaSeq: [],
      rawmidi: [],
      mdnsRemotes: [],
      peers,
    });
    expect(endpoints).toHaveLength(1);
    expect(endpoints[0].id).toBe(endpointIdForHost("192.168.1.80", 5004));
    expect(endpoints[0].peerId).toBe(7);
    expect(endpoints[0].label).toBe("DeepMind 12D");
  });
});

describe("mergeDeviceList manual rtp host merge", () => {
  it("merges manual registry onto live host endpoint by hostname/port", () => {
    const registry: RegistryDevice = {
      identity: "rtpmidi_client:hostname=192.168.1.80,service=DeepMind 12D,port=5004",
      type: "rtpmidi_client",
      name: "DeepMind 12D",
      source: "manual",
      firstSeen: 1,
      lastSeen: 2,
      online: false,
    };
    const peers: RouterPeer[] = [
      {
        id: 7,
        name: "WEB · 192.168.1.80",
        type: "peer_device_rtpmidi_client_t",
        send_to: [],
        recv: 5,
        sent: 2,
        raw: {
          connect_hostname: "192.168.1.80",
          connect_port: "5004",
        },
      },
    ];
    const endpoints = buildEndpoints({
      alsaSeq: [],
      rawmidi: [],
      mdnsRemotes: [],
      peers,
    });
    const rows = mergeDeviceList({
      endpoints,
      registryDevices: [registry],
      registryEnabled: true,
      peers,
      alsaSeq: [],
    });
    expect(rows).toHaveLength(1);
    expect(rows[0].isOfflineOnly).toBe(false);
    expect(rows[0].peerId).toBe(7);
    expect(rows[0].registry?.source).toBe("manual");
    expect(rows[0].label).toBe("DeepMind 12D");
    expect(rows[0].id).toBe(endpointIdForHost("192.168.1.80", 5004));
  });
});

describe("mergeDeviceList", () => {
  const endpoint: Endpoint = {
    id: "peer:5",
    kind: "peer",
    label: "Peak In",
    sub: "peer_device_alsa_seq_t",
    peerId: 5,
  };

  it("merges registry onto endpoint by peer id", () => {
    const registry: RegistryDevice = {
      identity: "alsa_seq:client=Peak,port=In",
      type: "alsa_seq",
      name: "Peak In",
      source: "discovered",
      firstSeen: 1,
      lastSeen: 2,
      online: true,
      peerId: 5,
    };
    const rows = mergeDeviceList({
      endpoints: [endpoint],
      registryDevices: [registry],
      registryEnabled: true,
      peers: [],
      alsaSeq: [],
    });
    expect(rows).toHaveLength(1);
    expect(rows[0].registry?.identity).toBe(registry.identity);
    expect(rows[0].sourceTag).toBe("Discovered");
    expect(rows[0].statusTag).toBe("online");
  });

  it("adds offline-only registry rows", () => {
    const registry: RegistryDevice = {
      identity: "rtpmidi_client:hostname=pi.local,service=Synth",
      type: "rtpmidi_client",
      name: "Synth",
      source: "manual",
      firstSeen: 1,
      lastSeen: 100,
      online: false,
    };
    const rows = mergeDeviceList({
      endpoints: [],
      registryDevices: [registry],
      registryEnabled: true,
      peers: [],
      alsaSeq: [],
    });
    expect(rows).toHaveLength(1);
    expect(rows[0].isOfflineOnly).toBe(true);
    expect(rows[0].connectEndpointId).toBe("host:pi.local:5004");
    expect(rows[0].id).toBe(registryCardId(registry.identity));
    expect(rows[0].statusTag).toBe("offline");
    expect(rows[0].sourceTag).toBe("Manual");
  });

  it("tags peer-only rows as Session when registry enabled", () => {
    const rows = mergeDeviceList({
      endpoints: [endpoint],
      registryDevices: [],
      registryEnabled: true,
      peers: [],
      alsaSeq: [],
    });
    expect(rows[0].sourceTag).toBe("Session");
    expect(rows[0].registry).toBeNull();
  });
});
