import { describe, expect, it, beforeEach } from "vitest";
import { daemonStore } from "./store";
import { deviceFromIdentity } from "./midiEnumerate";
import type { DeviceRow } from "./midiEnumerate";

// ── Direct unit tests for identity matching ─────────────────────────

function identityPrefix(identity: string): string {
  const colon = identity.indexOf(":");
  return colon > 0 ? identity.slice(0, colon) : "";
}
function identityFields(identity: string): Record<string, string> {
  const colon = identity.indexOf(":");
  const tail = colon > 0 ? identity.slice(colon + 1) : identity;
  const fields: Record<string, string> = {};
  for (const part of tail.split(",")) {
    const eq = part.indexOf("=");
    if (eq <= 0) continue;
    fields[part.slice(0, eq)] = part.slice(eq + 1);
  }
  return fields;
}
function deviceMatchesIdentity(entry: DeviceRow, identity: string): boolean {
  if (identityPrefix(identity) !== identityPrefix(entry.identity)) return false;
  const want = identityFields(identity);
  const have = identityFields(entry.identity);
  for (const [k, v] of Object.entries(want)) {
    if (have[k] !== v) return false;
  }
  return true;
}

function entry(id: string): DeviceRow {
  return deviceFromIdentity(id) ?? { identity: id, type: "", label: "" };
}

describe("deviceMatchesIdentity", () => {
  it("matches partial identity (c/p only vs full)", () => {
    const e = entry("alsa_seq:c=129,p=0,client=aseqdump,port=aseqdump");
    expect(deviceMatchesIdentity(e, "alsa_seq:c=129,p=0")).toBe(true);
  });

  it("matches exact identity", () => {
    const e = entry("alsa_seq:c=129,p=0,client=aseqdump,port=aseqdump");
    expect(deviceMatchesIdentity(e, "alsa_seq:c=129,p=0,client=aseqdump,port=aseqdump")).toBe(true);
  });

  it("rejects partial on missing field", () => {
    const e = entry("alsa_seq:c=129,p=0,client=aseqdump,port=aseqdump");
    expect(deviceMatchesIdentity(e, "alsa_seq:c=129,p=0,client=wrong")).toBe(false);
  });

  it("rejects different type prefix", () => {
    const e = entry("rawmidi:device=/dev/snd/midiC0D0");
    expect(deviceMatchesIdentity(e, "alsa_seq:c=129,p=0")).toBe(false);
  });
});

// ── Store tests ─────────────────────────────────────────────────────

function emptyState(): DaemonState {
  return {
    version: "",
    peers: [],
    routerRaw: [],
    mdns: { status: "—", announcements: [], remotes: [] },
  };
}

describe("daemonStore", () => {
  beforeEach(() => {
    // Reset store to empty state
    daemonStore.loadSnapshot({});
  });

  it("loads a status snapshot", () => {
    daemonStore.loadSnapshot({
      version: "1.0.0",
      router: [
        {
          id: 1,
          name: "Test peer",
          type: "fake",
          stats: { recv: 10, sent: 5 },
          send_to: [2],
        },
        {
          id: 2,
          name: "Target peer",
          type: "fake",
          stats: { recv: 5, sent: 10 },
          send_to: [],
        },
      ],
      mdns: {
        status: "Available",
        announcements: [],
        remote_announcements: [],
      },
    });

    const state = daemonStore.getState();
    expect(state.version).toBe("1.0.0");
    expect(state.peers).toHaveLength(2);
    expect(state.peers[0].name).toBe("Test peer");
    expect(state.peers[0].send_to).toEqual([2]);
    expect(state.mdns.status).toBe("Available");
  });

  it("handles peer_added event", () => {
    daemonStore.loadSnapshot({
      version: "1.0.0",
      router: [],
      mdns: {},
    });
    expect(daemonStore.getState().peers).toHaveLength(0);

    daemonStore.reduce({
      type: "peer_added",
      peer: {
        id: 42,
        name: "New peer",
        type: "fake",
        stats: { recv: 0, sent: 0 },
        send_to: [],
      },
    });

    const state = daemonStore.getState();
    expect(state.peers).toHaveLength(1);
    expect(state.peers[0].id).toBe(42);
    expect(state.peers[0].name).toBe("New peer");
  });

  it("handles peer_removed event", () => {
    daemonStore.loadSnapshot({
      version: "1.0.0",
      router: [
        { id: 1, name: "A", type: "fake", stats: { recv: 0, sent: 0 }, send_to: [2] },
        { id: 2, name: "B", type: "fake", stats: { recv: 0, sent: 0 }, send_to: [] },
      ],
      mdns: {},
    });
    expect(daemonStore.getState().peers).toHaveLength(2);

    daemonStore.reduce({ type: "peer_removed", peer_id: 1 });

    const state = daemonStore.getState();
    expect(state.peers).toHaveLength(1);
    expect(state.peers[0].id).toBe(2);
    // Edge from removed peer should be cleaned up
    expect(state.peers[0].send_to).toEqual([]);
  });

  it("handles edge_added event", () => {
    daemonStore.loadSnapshot({
      version: "1.0.0",
      router: [
        { id: 1, name: "A", type: "fake", stats: { recv: 0, sent: 0 }, send_to: [] },
        { id: 2, name: "B", type: "fake", stats: { recv: 0, sent: 0 }, send_to: [] },
      ],
      mdns: {},
    });

    daemonStore.reduce({ type: "edge_added", from: 1, to: 2 });

    const state = daemonStore.getState();
    const peerA = state.peers.find((p) => p.id === 1);
    expect(peerA?.send_to).toContain(2);
  });

  it("handles edge_removed event", () => {
    daemonStore.loadSnapshot({
      version: "1.0.0",
      router: [
        { id: 1, name: "A", type: "fake", stats: { recv: 0, sent: 0 }, send_to: [2] },
        { id: 2, name: "B", type: "fake", stats: { recv: 0, sent: 0 }, send_to: [] },
      ],
      mdns: {},
    });
    expect(daemonStore.getState().peers[0].send_to).toContain(2);

    daemonStore.reduce({ type: "edge_removed", from: 1, to: 2 });

    const state = daemonStore.getState();
    const peerA = state.peers.find((p) => p.id === 1);
    expect(peerA?.send_to).not.toContain(2);
  });

  it("handles mdns_discovered event", () => {
    daemonStore.loadSnapshot({
      version: "1.0.0",
      router: [],
      mdns: { status: "Available", announcements: [], remote_announcements: [] },
    });
    expect(daemonStore.getState().mdns.remotes).toHaveLength(0);

    daemonStore.reduce({
      type: "mdns_discovered",
      remote: { name: "Remote1", hostname: "host.local", ip: "192.168.1.1", port: 5004 },
    });

    expect(daemonStore.getState().mdns.remotes).toHaveLength(1);
    expect(daemonStore.getState().mdns.remotes[0].name).toBe("Remote1");

    // Duplicate should be ignored
    daemonStore.reduce({
      type: "mdns_discovered",
      remote: { name: "Remote1", hostname: "host.local", ip: "192.168.1.1", port: 5004 },
    });
    expect(daemonStore.getState().mdns.remotes).toHaveLength(1);
  });

  it("handles mdns_removed event", () => {
    daemonStore.loadSnapshot({
      version: "1.0.0",
      router: [],
      mdns: { status: "Available", announcements: [], remote_announcements: [] },
    });
    daemonStore.reduce({
      type: "mdns_discovered",
      remote: { name: "R1", hostname: "h1", ip: "1.2.3.4", port: 5004 },
    });
    daemonStore.reduce({
      type: "mdns_discovered",
      remote: { name: "R2", hostname: "h2", ip: "5.6.7.8", port: 5005 },
    });
    expect(daemonStore.getState().mdns.remotes).toHaveLength(2);

    daemonStore.reduce({
      type: "mdns_removed",
      name: "R1",
      address: "h1",
      port: 5004,
    });
    expect(daemonStore.getState().mdns.remotes).toHaveLength(1);
    expect(daemonStore.getState().mdns.remotes[0].name).toBe("R2");
  });

  it("supports subscription listener notifications", () => {
    let called = 0;
    const unsub = daemonStore.subscribe(() => {
      called++;
    });

    daemonStore.reduce({
      type: "peer_added",
      peer: { id: 1, name: "P", type: "fake", stats: { recv: 0, sent: 0 }, send_to: [] },
    });
    expect(called).toBe(1);

    daemonStore.reduce({ type: "peer_removed", peer_id: 1 });
    expect(called).toBe(2);

    unsub();

    daemonStore.reduce({
      type: "peer_added",
      peer: { id: 2, name: "Q", type: "fake", stats: { recv: 0, sent: 0 }, send_to: [] },
    });
    expect(called).toBe(2); // not called after unsubscribe
  });

  it("removes alsa_seq device by partial identity (c/p match)", () => {
    // Simulate initial device.list seeding with full identity
    daemonStore.seedAlsaSeq([{
      identity: "alsa_seq:c=129,p=0,client=aseqdump,port=aseqdump",
      type: "alsa_seq",
      label: "aseqdump",
      client_name: "aseqdump",
      port_name: "0",
      client: 129,
      port: 0,
    }]);
    expect(daemonStore.getState().alsaSeq).toHaveLength(1);

    // Verify the stored identity before remove
    const stored = daemonStore.getState().alsaSeq[0];
    expect(stored.identity).toBe("alsa_seq:c=129,p=0,client=aseqdump,port=aseqdump");

    // Simulate remove event with partial identity (no names)
    daemonStore.reduce({
      type: "device_updated",
      action: "removed",
      identity: "alsa_seq:c=129,p=0",
    });

    expect(daemonStore.getState().alsaSeq).toHaveLength(0);
  });

  it("removes alsa_seq device by full identity (exact match)", () => {
    daemonStore.seedAlsaSeq([{
      identity: "alsa_seq:c=129,p=0,client=aseqdump,port=aseqdump",
      type: "alsa_seq",
      label: "aseqdump",
      client_name: "aseqdump",
      port_name: "0",
      client: 129,
      port: 0,
    }]);

    daemonStore.reduce({
      type: "device_updated",
      action: "removed",
      identity: "alsa_seq:c=129,p=0,client=aseqdump,port=aseqdump",
    });

    expect(daemonStore.getState().alsaSeq).toHaveLength(0);
  });

  it("does not remove device when type prefix differs", () => {
    daemonStore.seedAlsaSeq([{
      identity: "rawmidi:device=/dev/snd/midiC0D0",
      type: "rawmidi",
      label: "/dev/snd/midiC0D0",
    }]);

    daemonStore.reduce({
      type: "device_updated",
      action: "removed",
      identity: "alsa_seq:c=129,p=0",
    });

    expect(daemonStore.getState().alsaSeq).toHaveLength(1);
  });
});
