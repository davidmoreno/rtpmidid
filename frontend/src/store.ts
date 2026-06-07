/**
 * Event-sourcing store for rtpmidid Web UI.
 *
 * Replaces the old polling-based `status` refresh. The store receives an
 * initial snapshot via the `status` RPC call, then applies incremental
 * events (`peer_added`, `peer_removed`, `edge_added`, etc.) from the
 * WebSocket subscription.
 */

import type { StatusResult } from "./tabs/types";
import { normalizePeers, parseMdns, type RouterPeer, type MdnsRemote } from "./model";

// ── Event types ────────────────────────────────────────────────────────────

export type DaemonEvent =
  | { type: "status_snapshot"; status: StatusResult }
  | { type: "peer_added"; peer: Record<string, unknown> }
  | { type: "peer_removed"; peer_id: number }
  | { type: "peer_stats"; peer_id: number; recv: number; sent: number }
  | { type: "edge_added"; from: number; to: number }
  | { type: "edge_removed"; from: number; to: number }
  | { type: "mdns_discovered"; remote: MdnsRemote }
  | { type: "mdns_removed"; name: string; address: string; port: number };

// ── Daemon state ───────────────────────────────────────────────────────────

export interface DaemonState {
  version: string;
  peers: RouterPeer[];
  routerRaw: Record<string, unknown>[];
  mdns: ReturnType<typeof parseMdns>;
  web: { url: string; accessible: boolean };
}

// ── Store ──────────────────────────────────────────────────────────────────

type Listener = () => void;

function createDaemonStore() {
  let state: DaemonState = {
    version: "",
    peers: [],
    routerRaw: [],
    mdns: { status: "—", announcements: [], remotes: [] },
    web: { url: "", accessible: false },
  };

  const listeners = new Set<Listener>();

  function getState(): DaemonState {
    return state;
  }

  function subscribe(fn: Listener): () => void {
    listeners.add(fn);
    return () => listeners.delete(fn);
  }

  function notify() {
    for (const fn of listeners) {
      fn();
    }
  }

  function reduce(event: DaemonEvent) {
    switch (event.type) {
      case "status_snapshot": {
        const st = event.status;
        const routerRaw = (st.router ?? []) as Record<string, unknown>[];
        state = {
          version: st.version ?? "",
          peers: normalizePeers(routerRaw),
          routerRaw,
          mdns: parseMdns(st.mdns),
          web: (st.web as DaemonState["web"]) ?? { url: "", accessible: false },
        };
        break;
      }

      case "peer_added": {
        const raw = event.peer;
        const newPeers = normalizePeers([raw]);
        const newPeer = newPeers[0];
        if (!newPeer) break;

        // Replace if exists, otherwise append
        const idx = state.peers.findIndex((p) => p.id === newPeer.id);
        if (idx >= 0) {
          state.peers[idx] = newPeer;
        } else {
          state.peers = [...state.peers, newPeer];
        }

        // Update routerRaw
        const rawIdx = state.routerRaw.findIndex(
          (r) => Number(r.id) === newPeer.id,
        );
        if (rawIdx >= 0) {
          state.routerRaw[rawIdx] = raw;
        } else {
          state.routerRaw = [...state.routerRaw, raw];
        }
        break;
      }

      case "peer_removed": {
        state.peers = state.peers.filter((p) => p.id !== event.peer_id);
        state.routerRaw = state.routerRaw.filter(
          (r) => Number(r.id) !== event.peer_id,
        );
        // Remove edges referencing this peer
        for (const p of state.peers) {
          p.send_to = p.send_to.filter((tid) => tid !== event.peer_id);
        }
        break;
      }

      case "peer_stats": {
        // Lightweight: update just the stats counters on an existing peer
        const idx = state.peers.findIndex((p) => p.id === event.peer_id);
        if (idx >= 0) {
          const updated = { ...state.peers[idx] };
          updated.recv = event.recv;
          updated.sent = event.sent;
          updated.raw = {
            ...updated.raw,
            stats: { recv: event.recv, sent: event.sent },
          };
          state.peers[idx] = updated;
        }
        break;
      }

      case "edge_added": {
        const fromPeer = state.peers.find((p) => p.id === event.from);
        if (fromPeer && !fromPeer.send_to.includes(event.to)) {
          fromPeer.send_to = [...fromPeer.send_to, event.to];
        }
        break;
      }

      case "edge_removed": {
        const fromPeer = state.peers.find((p) => p.id === event.from);
        if (fromPeer) {
          fromPeer.send_to = fromPeer.send_to.filter(
            (tid) => tid !== event.to,
          );
        }
        break;
      }

      case "mdns_discovered": {
        const remotes = [...state.mdns.remotes];
        const exists = remotes.some(
          (r) =>
            r.name === event.remote.name &&
            r.hostname === event.remote.hostname &&
            String(r.port) === String(event.remote.port),
        );
        if (!exists) {
          remotes.push(event.remote);
          state.mdns = { ...state.mdns, remotes };
        }
        break;
      }

      case "mdns_removed": {
        const remotes = state.mdns.remotes.filter(
          (r) =>
            !(
              r.name === event.name &&
              r.hostname === event.address &&
              Number(r.port) === event.port
            ),
        );
        state.mdns = { ...state.mdns, remotes };
        break;
      }
    }

    notify();
  }

  /** Subscribe to the `status_snapshot` state, applying the initial snapshot. */
  function loadSnapshot(status: StatusResult) {
    reduce({ type: "status_snapshot", status });
  }

  return { getState, subscribe, reduce, loadSnapshot };
}

/** Singleton store for the daemon state. */
export const daemonStore = createDaemonStore();

// ── Preact hook ────────────────────────────────────────────────────────────

import { useState, useEffect } from "preact/hooks";

/** Subscribe a Preact component to the daemon store. */
export function useDaemonState(): DaemonState {
  const [, setTick] = useState(0);
  useEffect(() => {
    return daemonStore.subscribe(() => setTick((n) => n + 1));
  }, []);
  // Always read latest from the store (not via closure)
  // We use the tick to trigger re-renders, but read state directly.
  return daemonStore.getState();
}
