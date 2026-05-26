import { describe, expect, it } from "vitest";
import {
  alsaStableIdFromNames,
  buildAlsaSubscriptionConnections,
  buildConnections,
  parseAlsaSubscriptions,
  peerStableId,
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

describe("alsaStableIdFromNames", () => {
  it("escapes colons in client/port names so the id parses round-trippably", () => {
    expect(alsaStableIdFromNames("My:Synth", "Port:1")).toBe(
      "alsa:My|Synth:Port|1",
    );
  });
  it("returns undefined when either name is empty", () => {
    expect(alsaStableIdFromNames("", "Port")).toBeUndefined();
    expect(alsaStableIdFromNames("Client", "")).toBeUndefined();
  });
});

describe("parseAlsaSubscriptions", () => {
  it("ignores rows that miss any of the four required numeric addresses", () => {
    const raw = [
      { from_client: 1, from_port: 0, to_client: 2 }, // missing to_port
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

  it("populates stableId on both sides so the row can be saved", () => {
    const subs = [sub({ c: 1, p: 0, cn: "A", pn: "P1" }, { c: 2, p: 0, cn: "B", pn: "P2" })];
    const rows = buildAlsaSubscriptionConnections(subs, []);
    expect(rows[0].from.stableId).toBe("alsa:A:P1");
    expect(rows[0].to.stableId).toBe("alsa:B:P2");
    expect(rows[0].from.endpointId).toBe("alsa:1:0");
    expect(rows[0].to.endpointId).toBe("alsa:2:0");
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

  it("links the ALSA address to a router peer id when local_alsa_peer_t hangs off it", () => {
    const peer: RouterPeer = {
      id: 7,
      name: "Synth bridge",
      type: "local_alsa_peer_t",
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

describe("peerStableId", () => {
  it("matches the daemon's local_alsa_peer_t format", () => {
    const peer: RouterPeer = {
      id: 5,
      name: "ALSA: Synth",
      type: "local_alsa_peer_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {
        alsa_subscribe_from: { client: 128, port: 0, client_name: "My:Synth", port_name: "Out" },
      },
    };
    expect(peerStableId(peer)).toBe("alsa:My|Synth:Out");
  });

  it("returns undefined for peer types that have no stable identity", () => {
    const peer: RouterPeer = {
      id: 9,
      name: "Web monitor",
      type: "webui_midi_monitor_peer_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {},
    };
    expect(peerStableId(peer)).toBeUndefined();
  });

  /* The following tests cover the new fallback paths added so users see ☆
     instead of the disabled marker on more rows. Each fallback IS lossy
     (relies on configured names rather than structural fields), but for
     INI/RPC-configured peers the name is stable across restarts. */

  it("falls back to alsa_local:<name> for local_alsa_peer_t without alsa_subscribe_from", () => {
    const peer: RouterPeer = {
      id: 12,
      name: "Network Export",
      type: "local_alsa_peer_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: { name: "Network Export" },
    };
    expect(peerStableId(peer)).toBe("alsa_local:Network Export");
  });

  it("treats hostname=\"null\" as empty so we don't burn the sentinel into a stable id", () => {
    /* network_address_t formats unresolved sockaddrs as the literal "null".
       Falling back to the peer name keeps the row saveable. */
    const peer: RouterPeer = {
      id: 15,
      name: "MyClient",
      type: "network_rtpmidi_client_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {
        connect_hostname: "null",
        peer: { remote: { hostname: "null", name: "" } },
      },
    };
    expect(peerStableId(peer)).toBe("rtpmidi_client_named:MyClient");
  });

  it("uses alsa_listener_named:<name> when the listener name doesn't follow the ' <-> ' convention", () => {
    const peer: RouterPeer = {
      id: 20,
      name: "Custom Listener Name",
      type: "local_alsa_listener_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: { name: "Custom Listener Name" },
    };
    expect(peerStableId(peer)).toBe("alsa_listener_named:Custom Listener Name");
  });

  it("provides a generic <short_type>:<name> id for unknown peer types so saves still work", () => {
    const peer: RouterPeer = {
      id: 30,
      name: "MyPeer",
      type: "future_peer_kind_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: { name: "MyPeer" },
    };
    expect(peerStableId(peer)).toBe("future_peer_kind:MyPeer");
  });

  it("never returns a stable id for the session-bound monitor sink even when it has a name", () => {
    const peer: RouterPeer = {
      id: 40,
      name: "monitor-uuid-deadbeef",
      type: "webui_midi_monitor_peer_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: { name: "monitor-uuid-deadbeef" },
    };
    expect(peerStableId(peer)).toBeUndefined();
  });
});

describe("buildConnections (aggregate rows dropped)", () => {
  /* `rtp_srv:` and `rtp_link:` rows used to be emitted to visualise an RTP
     listener's remotes and an RTP client's connection-string respectively.
     They duplicated info already shown in the Devices tab and their synthetic
     "to" side could never be saved, so we now drop them entirely. */
  it("does not emit an aggregate row for network_rtpmidi_listener_t", () => {
    const listener: RouterPeer = {
      id: 1,
      name: "rtpmidid",
      type: "network_rtpmidi_listener_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: { name: "rtpmidid", peers: [] },
    };
    const rows = buildConnections([listener]);
    expect(rows).toHaveLength(0);
  });

  it("does not emit an aggregate row for network_rtpmidi_client_t", () => {
    const client: RouterPeer = {
      id: 2,
      name: "Synth",
      type: "network_rtpmidi_client_t",
      send_to: [],
      recv: 0,
      sent: 0,
      raw: {
        name: "Synth",
        connect_hostname: "192.168.1.5",
        connect_port: 5004,
      },
    };
    const rows = buildConnections([client]);
    expect(rows).toHaveLength(0);
  });

  it("still emits router edges between peers (those represent real saveable connections)", () => {
    const a: RouterPeer = {
      id: 3,
      name: "A",
      type: "local_alsa_peer_t",
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
      type: "local_alsa_peer_t",
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
  });
});

describe("mergeConnectionsWithPersisted (alsaseq path)", () => {
  it("marks an alsaseq row as persisted when both sides stable ids match a saved pair", () => {
    const subs = [sub({ c: 1, p: 0, cn: "A", pn: "P1" }, { c: 2, p: 0, cn: "B", pn: "P2" })];
    const live = buildAlsaSubscriptionConnections(subs, []);
    const merged = mergeConnectionsWithPersisted(
      live,
      [{ side_a: "alsa:A:P1", side_b: "alsa:B:P2" }],
      [],
    );
    /* One live row + zero saved-only rows (live matched the saved pair). */
    expect(merged).toHaveLength(1);
    expect(merged[0].persisted).toBe(true);
    expect(merged[0].canRemoveFromDb).toBe(true);
    expect(merged[0].persistedSideA).toBe("alsa:A:P1");
    expect(merged[0].persistedSideB).toBe("alsa:B:P2");
  });

  it("offers + (canAddToDb) for live alsaseq rows that have no matching saved pair", () => {
    const subs = [sub({ c: 1, p: 0, cn: "A", pn: "P1" }, { c: 2, p: 0, cn: "B", pn: "P2" })];
    const live = buildAlsaSubscriptionConnections(subs, []);
    const merged = mergeConnectionsWithPersisted(live, [], []);
    expect(merged[0].canAddToDb).toBe(true);
    expect(merged[0].persisted).toBeUndefined();
  });

  it("classifies a saved-only alsa pair as alsaseq (not midirouter)", () => {
    const merged = mergeConnectionsWithPersisted(
      [],
      [{ side_a: "alsa:Alpha:OUT", side_b: "alsa:Beta:IN" }],
      [],
    );
    expect(merged).toHaveLength(1);
    expect(merged[0].type).toBe("alsaseq");
    expect(merged[0].persisted).toBe(true);
  });
});

describe("annotateLiveOnly / cannotSaveReason", () => {
  it("flags router rows whose endpoint has no stable identity with a tooltip reason", () => {
    /* Monitor peer (no stable id) wired to a local ALSA peer. */
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
      type: "local_alsa_peer_t",
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
    expect(annotated[0].cannotSaveReason).toMatch(/stable identity/i);
  });
});
