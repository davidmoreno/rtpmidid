import { describe, expect, it } from "vitest";
import { identityFromAlsaNames, identityFromPeerRow } from "./deviceIdentity";
import {
  buildAlsaSubscriptionConnections,
  buildConnections,
  parseAlsaSubscriptions,
  type AlsaSubscriptionRaw,
  type RouterPeer,
} from "./model";
import {
  mergeConnectionsWithPersisted,
  annotateLiveOnly,
} from "./persistedConnections";

const sub = (
  from: { c: number; p: number; cn?: string; pn?: string },
  to: { c: number; p: number; cn?: string; pn?: string },
): AlsaSubscriptionRaw => ({
  from_client: from.c,
  from_port: from.p,
  to_client: to.c,
  to_port: to.p,
  from_client_name: from.cn,
  from_port_name: from.pn,
  to_client_name: to.cn,
  to_port_name: to.pn,
});

describe("identityFromAlsaNames", () => {
  it("returns undefined when either name is empty", () => {
    expect(identityFromAlsaNames("", "Port")).toBeNull();
    expect(identityFromAlsaNames("Client", "")).toBeNull();
  });
});

describe("parseAlsaSubscriptions", () => {
  it("ignores rows that miss any of the four required numeric addresses", () => {
    const raw = [
      { from_client: 1, from_port: 0, to_client: 2 },
      sub({ c: 1, p: 0 }, { c: 2, p: 1 }),
    ];
    expect(parseAlsaSubscriptions(raw)).toHaveLength(1);
  });
});

describe("buildAlsaSubscriptionConnections", () => {
  it("merges opposite-direction subs into a single bidi row", () => {
    const subs = [
      sub({ c: 14, p: 0, cn: "Midi Through", pn: "Port-0" }, { c: 128, p: 0, cn: "Synth", pn: "Out" }),
      sub({ c: 128, p: 0, cn: "Synth", pn: "Out" }, { c: 14, p: 0, cn: "Midi Through", pn: "Port-0" }),
    ];
    const rows = buildAlsaSubscriptionConnections(subs, []);
    expect(rows).toHaveLength(1);
    expect(rows[0].bidirectional).toBe(true);
    expect(rows[0].type).toBe("alsaseq");
    expect(rows[0].direction).toBe("↔");
  });

  it("keeps one-way subs as a single → row", () => {
    const subs = [
      sub({ c: 14, p: 0, cn: "Through", pn: "Port-0" }, { c: 128, p: 0, cn: "Synth", pn: "Out" }),
    ];
    const rows = buildAlsaSubscriptionConnections(subs, []);
    expect(rows).toHaveLength(1);
    expect(rows[0].bidirectional).toBeUndefined();
    expect(rows[0].direction).toBe("→");
  });

  it("populates identity on both sides so the row can be saved", () => {
    const subs = [sub({ c: 1, p: 0, cn: "A", pn: "P1" }, { c: 2, p: 0, cn: "B", pn: "P2" })];
    const rows = buildAlsaSubscriptionConnections(subs, []);
    expect(rows[0].from.identity).toBe("alsa_seq:client=A,port=P1");
    expect(rows[0].to.identity).toBe("alsa_seq:client=B,port=P2");
  });

  it("uses the daemon-provided client/port names to build labels even when only one row carries them", () => {
    const subs = [
      sub({ c: 1, p: 0, cn: "A", pn: "P1" }, { c: 2, p: 0, cn: "B", pn: "P2" }),
      sub({ c: 2, p: 0 }, { c: 1, p: 0 }),
    ];
    const rows = buildAlsaSubscriptionConnections(subs, []);
    expect(rows[0].bidirectional).toBe(true);
    expect(rows[0].from.label).toMatch(/A.*P1/);
    expect(rows[0].to.label).toMatch(/B.*P2/);
  });

  it("links the ALSA address to a router peer id when peer_device_alsa_seq_t hangs off it", () => {
    const peer: RouterPeer = {
      id: 7,
      name: "Synth bridge",
      type: "peer_device_alsa_seq_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {
        alsa_subscribe_from: { client: 128, port: 0, client_name: "Synth", port_name: "Out" },
      },
    };
    const subs = [sub({ c: 14, p: 0, cn: "Through", pn: "Port-0" }, { c: 128, p: 0, cn: "Synth", pn: "Out" })];
    const rows = buildAlsaSubscriptionConnections(subs, [peer]);
    expect(rows[0].to.peerId).toBe(7);
  });
});

describe("identityFromPeerRow", () => {
  it("matches alsa_seq from peer_device_alsa_seq_t", () => {
    const peer: RouterPeer = {
      id: 5,
      name: "ALSA: Synth",
      type: "peer_device_alsa_seq_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {
        alsa_subscribe_from: { client: 128, port: 0, client_name: "My:Synth", port_name: "Out" },
      },
    };
    expect(identityFromPeerRow(peer)).toBe("alsa_seq:client=My\\:Synth,port=Out");
  });

  it("returns null for webui monitor peer", () => {
    const peer: RouterPeer = {
      id: 9,
      name: "Web monitor",
      type: "webui_midi_monitor_peer_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {},
    };
    expect(identityFromPeerRow(peer)).toBeNull();
  });
});

describe("buildConnections (aggregate rows dropped)", () => {
  it("does not emit an aggregate row for peer_export_rtpmidi_server_t", () => {
    const listener: RouterPeer = {
      id: 1,
      name: "rtpmidid",
      type: "peer_export_rtpmidi_server_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: { name: "rtpmidid", peers: [] },
    };
    const rows = buildConnections([listener]);
    expect(rows).toHaveLength(0);
  });

  it("does not emit an aggregate row for peer_device_rtpmidi_client_t", () => {
    const client: RouterPeer = {
      id: 2,
      name: "Synth",
      type: "peer_device_rtpmidi_client_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {
        name: "Synth",
        connect_hostname: "192.168.1.5",
        connect_port: "5004",
      },
    };
    const rows = buildConnections([client]);
    expect(rows).toHaveLength(0);
  });

  it("still emits router edges between peers (those represent real saveable connections)", () => {
    const a: RouterPeer = {
      id: 3,
      name: "A",
      type: "peer_device_alsa_seq_t",
      send_to: [4],
      recv: 0,
      sent: 0,
      raw: {
        alsa_subscribe_from: { client: 128, port: 0, client_name: "A", port_name: "P" },
      },
    };
    const b: RouterPeer = {
      id: 4,
      name: "B",
      type: "peer_device_alsa_seq_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {
        alsa_subscribe_from: { client: 129, port: 0, client_name: "B", port_name: "P" },
      },
    };
    const rows = buildConnections([a, b]);
    expect(rows).toHaveLength(1);
    expect(rows[0].kind).toBe("router");
    expect(rows[0].from.identity).toBe("alsa_seq:client=A,port=P");
    expect(rows[0].to.identity).toBe("alsa_seq:client=B,port=P");
  });
});

describe("mergeConnectionsWithPersisted (alsaseq path)", () => {
  it("marks an alsaseq row as persisted when both sides match a saved pair", () => {
    const subs = [sub({ c: 1, p: 0, cn: "A", pn: "P1" }, { c: 2, p: 0, cn: "B", pn: "P2" })];
    const live = buildAlsaSubscriptionConnections(subs, []);
    const merged = mergeConnectionsWithPersisted(
      live,
      [
        {
          side_a: "alsa_seq:client=A,port=P1",
          side_b: "alsa_seq:client=B,port=P2",
        },
      ],
      [],
    );
    expect(merged).toHaveLength(1);
    expect(merged[0].persisted).toBe(true);
    expect(merged[0].canRemoveFromDb).toBe(true);
    expect(merged[0].persistedSideA).toBe("alsa_seq:client=A,port=P1");
    expect(merged[0].persistedSideB).toBe("alsa_seq:client=B,port=P2");
  });

  it("offers + (canAddToDb) for live alsaseq rows that have no matching saved pair", () => {
    const subs = [sub({ c: 1, p: 0, cn: "A", pn: "P1" }, { c: 2, p: 0, cn: "B", pn: "P2" })];
    const live = buildAlsaSubscriptionConnections(subs, []);
    const merged = mergeConnectionsWithPersisted(live, [], []);
    expect(merged[0].canAddToDb).toBe(true);
    expect(merged[0].persisted).toBeUndefined();
  });

  it("classifies a saved-only alsa pair as alsaseq (legacy alsa: prefix)", () => {
    const merged = mergeConnectionsWithPersisted(
      [],
      [{ side_a: "alsa:Alpha:OUT", side_b: "alsa:Beta:IN" }],
      [],
    );
    expect(merged).toHaveLength(1);
    expect(merged[0].type).toBe("alsaseq");
    expect(merged[0].persisted).toBe(true);
  });

  it("classifies alsa_seq identity sides as alsaseq", () => {
    const merged = mergeConnectionsWithPersisted(
      [],
      [
        {
          side_a: "alsa_seq:client=Alpha,port=OUT",
          side_b: "alsa_seq:client=Beta,port=IN",
        },
      ],
      [],
    );
    expect(merged[0].type).toBe("alsaseq");
  });
});

describe("annotateLiveOnly / cannotSaveReason", () => {
  it("flags router rows whose endpoint has no device identity with a tooltip reason", () => {
    const monitor: RouterPeer = {
      id: 9,
      name: "Web monitor",
      type: "webui_midi_monitor_peer_t",
      send_to: [10],
      recv: 0,
      sent: 0,
      raw: {},
    };
    const alsa: RouterPeer = {
      id: 10,
      name: "Synth",
      type: "peer_device_alsa_seq_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {
        alsa_subscribe_from: { client: 128, port: 0, client_name: "S", port_name: "O" },
      },
    };
    const live = buildConnections([monitor, alsa]);
    const annotated = annotateLiveOnly(live);
    expect(annotated).toHaveLength(1);
    expect(annotated[0].canAddToDb).toBeUndefined();
    expect(annotated[0].cannotSaveReason).toMatch(/device identity/i);
  });
});
