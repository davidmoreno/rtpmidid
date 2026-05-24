import type { ConnectionParticipant, ConnectionRow, RouterPeer } from "./model";
import { formatStableIdLabel } from "./persistedConnectionsFormat";

export type { formatStableIdLabel } from "./persistedConnectionsFormat";

export type PersistedConnectionRow = {
  side_a: string;
  side_b: string;
  active_a?: boolean;
  active_b?: boolean;
  peer_a?: number;
  peer_b?: number;
};

export type ConnectionsListResult = {
  enabled?: boolean;
  connections?: PersistedConnectionRow[];
};

/** Server sends `enabled` as JSON number 1 (dm-json int32), not boolean true. */
export function connectionsDbEnabledFromList(raw: unknown): boolean {
  return parseConnectionsListResult(raw).enabled === true;
}

export function parseConnectionsListResult(raw: unknown): ConnectionsListResult {
  if (!raw || typeof raw !== "object") return { enabled: false, connections: [] };
  const o = raw as Record<string, unknown>;
  const enabled = o.enabled === true || o.enabled === 1;
  const rows = Array.isArray(o.connections) ? o.connections : [];
  const connections: PersistedConnectionRow[] = [];
  for (const r of rows) {
    if (!r || typeof r !== "object") continue;
    const row = r as Record<string, unknown>;
    const side_a = typeof row.side_a === "string" ? row.side_a : "";
    const side_b = typeof row.side_b === "string" ? row.side_b : "";
    if (!side_a || !side_b) continue;
    const peer_a =
      typeof row.peer_a === "number"
        ? row.peer_a
        : typeof row.peer_a === "string"
          ? Number(row.peer_a)
          : undefined;
    const peer_b =
      typeof row.peer_b === "number"
        ? row.peer_b
        : typeof row.peer_b === "string"
          ? Number(row.peer_b)
          : undefined;
    connections.push({
      side_a,
      side_b,
      active_a: row.active_a === true || row.active_a === 1,
      active_b: row.active_b === true || row.active_b === 1,
      peer_a: Number.isFinite(peer_a) ? peer_a : undefined,
      peer_b: Number.isFinite(peer_b) ? peer_b : undefined,
    });
  }
  return { enabled, connections };
}

export function persistedPairKey(a: string, b: string): string {
  return a < b ? `${a}\0${b}` : `${b}\0${a}`;
}

function peerName(peers: RouterPeer[], id: number): string {
  const p = peers.find((x) => x.id === id);
  return p ? (p.name.trim() || `#${p.id}`) : `#${id}`;
}

function participantForSide(
  stableId: string,
  peerId: number | undefined,
  active: boolean | undefined,
  peers: RouterPeer[],
): ConnectionParticipant {
  if (active && peerId !== undefined && Number.isFinite(peerId)) {
    return { id: peerId, name: peerName(peers, peerId) };
  }
  return {
    id: 0,
    name: formatStableIdLabel(stableId),
    unavailable: true,
  };
}

function savedMatchesLiveRow(row: ConnectionRow, saved: PersistedConnectionRow): boolean {
  const pa = saved.peer_a;
  const pb = saved.peer_b;
  if (pa === undefined || pb === undefined) return false;
  const ids = row.participantRouterIds;
  return ids.includes(pa) && ids.includes(pb);
}

function annotateLiveParticipants(
  row: ConnectionRow,
  saved: PersistedConnectionRow,
  peers: RouterPeer[],
): ConnectionParticipant[] {
  const sideForPeer = (peerId: number): string | undefined => {
    if (saved.peer_a === peerId) return saved.side_a;
    if (saved.peer_b === peerId) return saved.side_b;
    return undefined;
  };
  return row.participantPeers.map((pp) => {
    if (pp.id === 0) return pp;
    const side = sideForPeer(pp.id);
    const active =
      side === saved.side_a
        ? saved.active_a
        : side === saved.side_b
          ? saved.active_b
          : true;
    if (active) return pp;
    return {
      id: pp.id,
      name: side ? formatStableIdLabel(side) : pp.name,
      unavailable: true,
    };
  });
}

function buildSavedOnlyRow(
  saved: PersistedConnectionRow,
  peers: RouterPeer[],
): ConnectionRow {
  const labelA = formatStableIdLabel(saved.side_a);
  const labelB = formatStableIdLabel(saved.side_b);
  const parts = [
    participantForSide(saved.side_a, saved.peer_a, saved.active_a, peers),
    participantForSide(saved.side_b, saved.peer_b, saved.active_b, peers),
  ];
  const routerIds = [saved.peer_a, saved.peer_b].filter(
    (x): x is number => x !== undefined && Number.isFinite(x),
  );
  return {
    id: `saved:${saved.side_a}:${saved.side_b}`,
    kind: "router",
    kindLabel: "Saved",
    summary: `${labelA} ↔ ${labelB}`,
    direction: "↔",
    participantPeers: parts,
    participantRouterIds: routerIds,
    nParticipants: 2,
    trafficTotal: 0,
    recvSum: 0,
    sentSum: 0,
    bidirectional: true,
    persisted: true,
    canRemoveFromDb: true,
    persistedSideA: saved.side_a,
    persistedSideB: saved.side_b,
  };
}

/** Merge live router/RTP rows with SQLite saved pairs (one row per saved edge). */
export function mergeConnectionsWithPersisted(
  live: ConnectionRow[],
  saved: PersistedConnectionRow[],
  peers: RouterPeer[],
): ConnectionRow[] {
  const matched = new Set<string>();
  const out: ConnectionRow[] = [];

  for (const row of live) {
    const hit = saved.find((s) => savedMatchesLiveRow(row, s));
    if (hit) {
      matched.add(persistedPairKey(hit.side_a, hit.side_b));
      out.push({
        ...row,
        persisted: true,
        canRemoveFromDb: true,
        persistedSideA: hit.side_a,
        persistedSideB: hit.side_b,
        participantPeers: annotateLiveParticipants(row, hit, peers),
      });
    } else {
      out.push(row);
    }
  }

  for (const s of saved) {
    const key = persistedPairKey(s.side_a, s.side_b);
    if (matched.has(key)) continue;
    out.push(buildSavedOnlyRow(s, peers));
  }

  return out;
}
