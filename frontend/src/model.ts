/** Normalized shapes for `status` JSON from rtpmidid. */

export type LatencyTriple = { last?: number; average?: number; stddev?: number };

export type RouterPeer = {
  id: number;
  name: string;
  type: string;
  send_to: number[];
  recv: number;
  sent: number;
  internal?: {
    until?: LatencyTriple;
    sendMidi?: LatencyTriple;
  };
  /** RTP CK latency when present */
  network?: LatencyTriple;
  raw: Record<string, unknown>;
};

function num(v: unknown): number {
  if (typeof v === "number" && !Number.isNaN(v)) return v;
  if (typeof v === "string") {
    const n = Number(v);
    return Number.isNaN(n) ? 0 : n;
  }
  return 0;
}

function triple(o: unknown): LatencyTriple | undefined {
  if (!o || typeof o !== "object") return undefined;
  const r = o as Record<string, unknown>;
  return {
    last: num(r.last),
    average: num(r.average),
    stddev: num(r.stddev),
  };
}

/** RTP `latency_ms` from `peer` or first of `peers[]`. */
function extractNetworkLatency(p: Record<string, unknown>): LatencyTriple | undefined {
  const peer = p.peer as Record<string, unknown> | undefined;
  if (peer?.latency_ms) return triple(peer.latency_ms);
  const peers = p.peers as unknown[] | undefined;
  if (peers?.length) {
    const first = peers[0] as Record<string, unknown>;
    if (first?.latency_ms) return triple(first.latency_ms);
  }
  return undefined;
}

function extractInternal(p: Record<string, unknown>): RouterPeer["internal"] {
  const il = p.internal_latency_ms as Record<string, unknown> | undefined;
  if (!il) return undefined;
  return {
    until: triple(il.until_send_midi_ms),
    sendMidi: triple(il.send_midi_ms),
  };
}

export function normalizePeers(router: unknown[]): RouterPeer[] {
  return router.map((row) => {
    const p = row as Record<string, unknown>;
    const stats = p.stats as Record<string, unknown> | undefined;
    const st = (p.send_to as number[]) ?? [];
    return {
      id: num(p.id),
      name: String(p.name ?? ""),
      type: String(p.type ?? ""),
      send_to: Array.isArray(st) ? st.map((x) => Number(x)) : [],
      recv: num(stats?.recv),
      sent: num(stats?.sent),
      internal: extractInternal(p),
      network: extractNetworkLatency(p),
      raw: p,
    };
  });
}

export type MdnsAnnouncement = { name: string; port: number | string };
export type MdnsRemote = {
  name: string;
  hostname: string;
  port: number | string;
};

export function parseMdns(mdns: Record<string, unknown> | undefined): {
  status: string;
  announcements: MdnsAnnouncement[];
  remotes: MdnsRemote[];
} {
  if (!mdns) {
    return { status: "—", announcements: [], remotes: [] };
  }
  const ann: MdnsAnnouncement[] = [];
  const rawA = mdns.announcements as unknown[] | undefined;
  if (Array.isArray(rawA)) {
    for (const x of rawA) {
      const o = x as Record<string, unknown>;
      ann.push({ name: String(o.name ?? ""), port: num(o.port) || String(o.port ?? "") });
    }
  }
  const rem: MdnsRemote[] = [];
  const rawR = mdns.remote_announcements as unknown[] | undefined;
  if (Array.isArray(rawR)) {
    for (const x of rawR) {
      const o = x as Record<string, unknown>;
      rem.push({
        name: String(o.name ?? ""),
        hostname: String(o.hostname ?? ""),
        port: num(o.port) || String(o.port ?? ""),
      });
    }
  }
  return {
    status: String(mdns.status ?? "—"),
    announcements: ann,
    remotes: rem,
  };
}

export type EdgeRow = {
  fromId: number;
  fromName: string;
  toId: number;
  toName: string;
};

export function buildEdges(peers: RouterPeer[]): EdgeRow[] {
  const byId = new Map(peers.map((p) => [p.id, p]));
  const out: EdgeRow[] = [];
  for (const p of peers) {
    for (const tid of p.send_to) {
      const to = byId.get(tid);
      out.push({
        fromId: p.id,
        fromName: p.name,
        toId: tid,
        toName: to?.name ?? "?",
      });
    }
  }
  return out;
}

/** RTP `latency_ms` blocks from `peer` and each `peers[]` entry in status JSON. */
function tripleIfPresent(lm: unknown): LatencyTriple | undefined {
  if (!lm || typeof lm !== "object") return undefined;
  const r = lm as Record<string, unknown>;
  if (r.last === undefined && r.average === undefined) return undefined;
  return {
    last: r.last !== undefined ? num(r.last) : undefined,
    average: r.average !== undefined ? num(r.average) : undefined,
    stddev: r.stddev !== undefined ? num(r.stddev) : undefined,
  };
}

function collectRtpTriples(raw: Record<string, unknown>): LatencyTriple[] {
  const out: LatencyTriple[] = [];
  const peer = raw.peer as Record<string, unknown> | undefined;
  if (peer?.latency_ms) {
    const t = tripleIfPresent(peer.latency_ms);
    if (t) out.push(t);
  }
  const arr = raw.peers as unknown[] | undefined;
  if (Array.isArray(arr)) {
    for (const x of arr) {
      const o = x as Record<string, unknown>;
      if (o.latency_ms) {
        const t = tripleIfPresent(o.latency_ms);
        if (t) out.push(t);
      }
    }
  }
  return out;
}

function minMaxField(
  triples: LatencyTriple[],
  field: "last" | "average",
): { min?: number; max?: number } {
  const vals = triples
    .map((t) => t[field])
    .filter((x): x is number => x !== undefined && !Number.isNaN(x));
  if (!vals.length) return {};
  return { min: Math.min(...vals), max: Math.max(...vals) };
}

function spanLast<T>(
  items: T[],
  get: (x: T) => number | undefined,
): { min?: number; max?: number } {
  const vals = items
    .map(get)
    .filter((x): x is number => x !== undefined && !Number.isNaN(x));
  if (!vals.length) return {};
  return { min: Math.min(...vals), max: Math.max(...vals) };
}

/** Label for RTP sub-peer / `peer` object (has `remote` from peer_status). */
function rtpRemoteLabel(obj: Record<string, unknown>): string {
  const r = obj.remote as Record<string, unknown> | undefined;
  if (!r) return "remote";
  const name = String(r.name ?? "").trim();
  const host = String(r.hostname ?? "").trim();
  const port = r.port !== undefined && r.port !== "" ? String(r.port) : "";
  if (name && host) return port ? `${name} @ ${host}:${port}` : `${name} @ ${host}`;
  if (host) return port ? `${host}:${port}` : host;
  return name || "remote";
}

export type ConnectionKind = "router" | "rtp_server" | "rtp_link";

export type ConnectionRow = {
  id: string;
  kind: ConnectionKind;
  kindLabel: string;
  summary: string;
  participants: string;
  /** Router peer ids when present (RTP remotes are not router peers). */
  participantRouterIds: number[];
  /** Logical party count (hub + remotes, or from→to, etc.). */
  nParticipants: number;
  /** Max internal until→send_midi (ms) across involved router peers. */
  intUntilMax?: number;
  intSendMax?: number;
  rtpLastMax?: number;
  rtpAvgMax?: number;
  trafficTotal: number;
  /** Present when this row merges A→B and B→A. */
  bidirectional?: boolean;
};

function pairKeyUnordered(a: number, b: number): string {
  return a < b ? `${a}:${b}` : `${b}:${a}`;
}

export function buildConnections(peers: RouterPeer[]): ConnectionRow[] {
  const byId = new Map(peers.map((p) => [p.id, p]));
  const out: ConnectionRow[] = [];

  const sendMap = new Map<number, number[]>();
  for (const p of peers) {
    sendMap.set(p.id, p.send_to);
  }

  const emittedUndirected = new Set<string>();

  const pushRouterRow = (
    parts: RouterPeer[],
    opts: {
      id: string;
      bidirectional: boolean;
      summary: string;
      participants: string;
      participantRouterIds: number[];
      nParticipants: number;
    },
  ) => {
    const triples: LatencyTriple[] = [];
    for (const x of parts) triples.push(...collectRtpTriples(x.raw));
    const rl = minMaxField(triples, "last");
    const ra = minMaxField(triples, "average");
    const intU = spanLast(parts, (x) => x.internal?.until?.last);
    const intS = spanLast(parts, (x) => x.internal?.sendMidi?.last);
    const traffic = parts.reduce((s, x) => s + x.recv + x.sent, 0);
    out.push({
      id: opts.id,
      kind: "router",
      kindLabel: opts.bidirectional ? "Router ↔" : "Router",
      summary: opts.summary,
      participants: opts.participants,
      participantRouterIds: opts.participantRouterIds,
      nParticipants: opts.nParticipants,
      intUntilMax: intU.max,
      intSendMax: intS.max,
      rtpLastMax: rl.max,
      rtpAvgMax: ra.max,
      trafficTotal: traffic,
      bidirectional: opts.bidirectional || undefined,
    });
  };

  for (const p of peers) {
    for (const tid of p.send_to) {
      const from = p.id;
      if (from === tid) continue;
      const toPeer = byId.get(tid);
      if (toPeer) {
        const hasReverse = sendMap.get(tid)?.includes(from) ?? false;
        if (hasReverse) {
          const ukey = pairKeyUnordered(from, tid);
          if (emittedUndirected.has(ukey)) continue;
          emittedUndirected.add(ukey);
          const lo = Math.min(from, tid);
          const hi = Math.max(from, tid);
          const pLo = byId.get(lo)!;
          const pHi = byId.get(hi)!;
          pushRouterRow([pLo, pHi], {
            id: `route_pair:${lo}:${hi}`,
            bidirectional: true,
            summary: `${lo}↔${hi}`,
            participants: `#${lo} ${pLo.name || "—"} ↔ #${hi} ${pHi.name || "—"}`,
            participantRouterIds: [lo, hi],
            nParticipants: 2,
          });
          continue;
        }
      }
      const to = toPeer;
      const parts = to ? [p, to] : [p];
      pushRouterRow(parts, {
        id: `route:${from}->${tid}`,
        bidirectional: false,
        summary: `${from}→${tid}`,
        participants: to
          ? `#${from} ${p.name || "—"} → #${tid} ${to.name || "—"}`
          : `#${from} ${p.name || "—"} → #${tid} ?`,
        participantRouterIds: to ? [from, tid] : [from, tid],
        nParticipants: to ? 2 : 1,
      });
    }
  }

  for (const p of peers) {
    if (
      p.type !== "network_rtpmidi_listener_t" &&
      p.type !== "network_rtpmidi_multi_listener_t"
    ) {
      continue;
    }
    const subs = (p.raw.peers as unknown[] | undefined) ?? [];
    const subLabels = subs.map((x) =>
      rtpRemoteLabel(x as Record<string, unknown>),
    );
    const triples = collectRtpTriples(p.raw);
    const rl = minMaxField(triples, "last");
    const ra = minMaxField(triples, "average");
    const intU = spanLast([p], (x) => x.internal?.until?.last);
    const intS = spanLast([p], (x) => x.internal?.sendMidi?.last);
    const hubName = p.name || String(p.raw.name ?? "");
    const subPart = subLabels.length ? ` · ${subLabels.join(" · ")}` : "";
    out.push({
      id: `rtp_srv:${p.id}`,
      kind: "rtp_server",
      kindLabel: "RTP server",
      summary: `${p.id}: ${hubName}`,
      participants: `#${p.id} ${hubName || "—"} (hub)${subPart}`,
      participantRouterIds: [p.id],
      nParticipants: 1 + subLabels.length,
      intUntilMax: intU.max,
      intSendMax: intS.max,
      rtpLastMax: rl.max,
      rtpAvgMax: ra.max,
      trafficTotal: p.recv + p.sent,
    });
  }

  for (const p of peers) {
    if (
      p.type !== "network_rtpmidi_client_t" &&
      p.type !== "network_rtpmidi_peer_t"
    ) {
      continue;
    }
    const triples = collectRtpTriples(p.raw);
    const rl = minMaxField(triples, "last");
    const ra = minMaxField(triples, "average");
    const intU = spanLast([p], (x) => x.internal?.until?.last);
    const intS = spanLast([p], (x) => x.internal?.sendMidi?.last);
    const peerObj = p.raw.peer as Record<string, unknown> | undefined;
    const rem = peerObj ? rtpRemoteLabel(peerObj) : "—";
    const kindLabel =
      p.type === "network_rtpmidi_client_t" ? "RTP client" : "RTP peer";
    out.push({
      id: `rtp_link:${p.id}`,
      kind: "rtp_link",
      kindLabel,
      summary: rem,
      participants: `#${p.id} ${p.name || "—"} · ${rem}`,
      participantRouterIds: [p.id],
      nParticipants: 2,
      intUntilMax: intU.max,
      intSendMax: intS.max,
      rtpLastMax: rl.max,
      rtpAvgMax: ra.max,
      trafficTotal: p.recv + p.sent,
    });
  }

  return out;
}
