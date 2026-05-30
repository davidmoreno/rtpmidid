import { describe, expect, it } from "vitest";
import {
  collectBridgeExportedEndpointIds,
  rtpPortsEqual,
} from "./endpoints";
import { identityFromMdnsGroup } from "./deviceIdentity";
import type { MdnsRemote, RouterPeer } from "./model";

function peer(
  id: number,
  type: string,
  name: string,
  raw: Record<string, unknown>,
): RouterPeer {
  return {
    id,
    name,
    type,
    send_to: [],
    recv: 0,
    sent: 0,
    raw: { name, ...raw },
  };
}

function mdnsIdentity(name: string, port: number): string {
  const g = {
    name,
    port,
    addresses: ["host.local"],
    ips: ["192.168.1.10"],
    instances: [
      {
        name,
        hostname: "host.local",
        ip: "192.168.1.10",
        port,
      } as MdnsRemote,
    ],
  };
  return identityFromMdnsGroup(g)!;
}

describe("rtpPortsEqual", () => {
  it("compares string and number ports", () => {
    expect(rtpPortsEqual(5004, 5004)).toBe(true);
    expect(rtpPortsEqual("5004", 5004)).toBe(true);
    expect(rtpPortsEqual(5004, "5004")).toBe(true);
    expect(rtpPortsEqual(5004, 5005)).toBe(false);
  });
});

describe("collectBridgeExportedEndpointIds", () => {
  const mdnsRemotes: MdnsRemote[] = [
    {
      name: "MyExport",
      hostname: "host.local",
      ip: "192.168.1.10",
      port: 5004,
    },
    {
      name: "OtherSynth",
      hostname: "else.local",
      ip: "192.168.1.11",
      port: 6000,
    },
  ];

  it("marks mdns identities that match peer_export_rtpmidi_server_t", () => {
    const peers: RouterPeer[] = [
      peer(1, "peer_export_rtpmidi_server_t", "MyExport", { port: 5004 }),
    ];
    const s = collectBridgeExportedEndpointIds(peers, mdnsRemotes);
    expect(s.has(mdnsIdentity("MyExport", 5004))).toBe(true);
    expect(s.has(mdnsIdentity("OtherSynth", 6000))).toBe(false);
  });

  it("marks mdns identities that match peer_import_rtpmidi_t listening.control_port", () => {
    const peers: RouterPeer[] = [
      peer(2, "peer_import_rtpmidi_t", "MyExport", {
        listening: { name: "MyExport", control_port: 5004, midi_port: 5005 },
      }),
    ];
    const s = collectBridgeExportedEndpointIds(peers, mdnsRemotes);
    expect(s.has(mdnsIdentity("MyExport", 5004))).toBe(true);
  });

  it("requires name match", () => {
    const peers: RouterPeer[] = [
      peer(1, "peer_export_rtpmidi_server_t", "WrongName", { port: 5004 }),
    ];
    const s = collectBridgeExportedEndpointIds(peers, mdnsRemotes);
    expect(s.has(mdnsIdentity("MyExport", 5004))).toBe(false);
  });
});
