import type {
  ConnectionEndpointRef,
  ConnectionParticipant,
  ConnectionRow,
  RouterPeer,
} from "./model";
import { formatStableIdLabel } from "./persistedConnectionsFormat";
import type { ConnectionDirection } from "./deviceIdentity";
import { directionArrow } from "./deviceIdentity";

export type { formatStableIdLabel } from "./persistedConnectionsFormat";

export type PersistedConnectionRow = {
  side_a: string;
  side_b: string;
  direction?: ConnectionDirection;
  enabled?: boolean;
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
    const dirRaw = typeof row.direction === "string" ? row.direction : "both";
    const direction: ConnectionDirection =
      dirRaw === "a2b" || dirRaw === "b2a" || dirRaw === "both" ? dirRaw : "both";
    const enabled = row.enabled !== false && row.enabled !== 0;
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
      direction,
      enabled,
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

/** True when a stored connection side is a pure-ALSA aconnect endpoint. */
export function isDirectAlsaSide(side: string): boolean {
  return side.startsWith("alsa:") || side.startsWith("alsa_seq:");
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
  /* Router rows: match by router peer ids when both sides resolve to a peer. */
  const pa = saved.peer_a;
  const pb = saved.peer_b;
  if (pa !== undefined && pb !== undefined) {
    const ids = row.participantRouterIds;
    if (ids.includes(pa) && ids.includes(pb)) return true;
  }
  /* Stable-id match: covers alsaseq rows (no router peer) and router rows
     whose live peers also expose a stable id. The db stores unordered pairs. */
  const fromStable = row.from.stableId;
  const toStable = row.to.stableId;
  if (fromStable && toStable) {
    if (
      (fromStable === saved.side_a && toStable === saved.side_b) ||
      (fromStable === saved.side_b && toStable === saved.side_a)
    ) {
      return true;
    }
  }
  return false;
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
  const sideRef = (
    stable: string,
    peerId: number | undefined,
    active: boolean | undefined,
    label: string,
  ): ConnectionEndpointRef => ({
    endpointId:
      active && peerId !== undefined && Number.isFinite(peerId)
        ? `peer:${peerId}`
        : stable,
    label,
    peerId:
      active && peerId !== undefined && Number.isFinite(peerId)
        ? peerId
        : undefined,
    stableId: stable,
    unavailable: !active,
  });
  /* "midirouter" vs "alsaseq" classification: every alsa stable id starts with
     `alsa:` (escape rules of compute_stable_id), so a pair where BOTH sides
     are alsa stable ids is a pure aconnect pair. Any other combination came
     from the router on a previous run, so render it as midirouter. */
  const isAlsa =
    isDirectAlsaSide(saved.side_a) && isDirectAlsaSide(saved.side_b);
  const dir = saved.direction ?? "both";
  const dirSymbol = directionArrow(dir);
  return {
    id: `saved:${saved.side_a}:${saved.side_b}`,
    kind: "router",
    type: isAlsa ? "alsaseq" : "midirouter",
    kindLabel: saved.enabled === false ? "Saved (disabled)" : "Saved",
    summary: `${labelA} ${dirSymbol} ${labelB}`,
    direction: dirSymbol,
    from: sideRef(saved.side_a, saved.peer_a, saved.active_a, labelA),
    to: sideRef(saved.side_b, saved.peer_b, saved.active_b, labelB),
    participantPeers: parts,
    participantRouterIds: routerIds,
    nParticipants: 2,
    trafficTotal: 0,
    recvSum: 0,
    sentSum: 0,
    bidirectional: dir === "both" || undefined,
    persisted: true,
    persistedEnabled: saved.enabled !== false,
    canRemoveFromDb: true,
    persistedSideA: saved.side_a,
    persistedSideB: saved.side_b,
  };
}

function classifyLiveRow(
  row: ConnectionRow,
  persisted: boolean,
): {
  canAddToDb?: boolean;
  cannotSaveReason?: string;
} {
  if (persisted) return {};
  const fromStable = row.from.stableId;
  const toStable = row.to.stableId;
  if (fromStable && toStable && fromStable !== toStable) {
    return { canAddToDb: true };
  }
  /* A side without a stable id is something the db cannot durably address
     (e.g. webui_midi_monitor_peer_t sink, router peers added at runtime that
     have not been named, transient RTP peers without name/host yet). Show the
     empty-circle indicator with the explanation below. */
  if (!fromStable && !toStable) {
    return {
      cannotSaveReason:
        "Neither endpoint has a stable identity; cannot persist this pair.",
    };
  }
  if (!fromStable) {
    return {
      cannotSaveReason: `"${row.from.label}" has no stable identity (e.g. monitor sink or unnamed peer); cannot persist this pair.`,
    };
  }
  if (!toStable) {
    return {
      cannotSaveReason: `"${row.to.label}" has no stable identity (e.g. monitor sink or unnamed peer); cannot persist this pair.`,
    };
  }
  return {
    cannotSaveReason: "Both endpoints share the same stable identity.",
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
        persistedEnabled: hit.enabled !== false,
        canRemoveFromDb: true,
        canAddToDb: false,
        cannotSaveReason: undefined,
        persistedSideA: hit.side_a,
        persistedSideB: hit.side_b,
        participantPeers: annotateLiveParticipants(row, hit, peers),
      });
    } else {
      const cls = classifyLiveRow(row, false);
      out.push({
        ...row,
        ...cls,
      });
    }
  }

  for (const s of saved) {
    const key = persistedPairKey(s.side_a, s.side_b);
    if (matched.has(key)) continue;
    out.push(buildSavedOnlyRow(s, peers));
  }

  return out;
}

/**
 * When the SQLite db is disabled, callers still want every live row to carry
 * the empty-circle/explanation for the DB column - apply the same
 * classification but never claim a row is persisted. */
export function annotateLiveOnly(live: ConnectionRow[]): ConnectionRow[] {
  return live.map((row) => ({ ...row, ...classifyLiveRow(row, false) }));
}
