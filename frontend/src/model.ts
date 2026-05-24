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

export type MdnsAnnouncementGroup = {
  name: string;
  port: number | string;
  /** Rows merged (same name+port). */
  count: number;
};

export type MdnsRemote = {
  name: string;
  hostname: string;
  /** Resolved IP from daemon (Avahi); may be empty on older daemons. */
  ip: string;
  port: number | string;
  /** Original JSON entry for extra fields in UI. */
  raw?: Record<string, unknown>;
};

/** One logical remote (name + port) with all resolved addresses / variants. */
export type MdnsRemoteGroup = {
  name: string;
  port: number | string;
  /** Unique mDNS hostnames from instances. */
  addresses: string[];
  /** Unique resolved IPs (what you connect to). */
  ips: string[];
  instances: MdnsRemote[];
};

export function groupMdnsAnnouncements(
  announcements: MdnsAnnouncement[],
): MdnsAnnouncementGroup[] {
  const map = new Map<string, MdnsAnnouncementGroup>();
  for (const a of announcements) {
    const key = `${a.name}\0${String(a.port)}`;
    const prev = map.get(key);
    if (prev) prev.count += 1;
    else map.set(key, { name: a.name, port: a.port, count: 1 });
  }
  return Array.from(map.values()).sort((x, y) => {
    const c = x.name.localeCompare(y.name);
    return c !== 0 ? c : String(x.port).localeCompare(String(y.port));
  });
}

export function groupMdnsRemotes(remotes: MdnsRemote[]): MdnsRemoteGroup[] {
  const map = new Map<string, MdnsRemoteGroup>();
  for (const r of remotes) {
    const key = `${r.name}\0${String(r.port)}`;
    let g = map.get(key);
    if (!g) {
      g = { name: r.name, port: r.port, addresses: [], ips: [], instances: [] };
      map.set(key, g);
    }
    g.instances.push(r);
    const h = r.hostname.trim();
    if (h && !g.addresses.includes(h)) g.addresses.push(h);
    const ip = r.ip.trim();
    if (ip && !g.ips.includes(ip)) g.ips.push(ip);
  }
  return Array.from(map.values()).sort((a, b) => {
    const c = a.name.localeCompare(b.name);
    return c !== 0 ? c : String(a.port).localeCompare(String(b.port));
  });
}

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
        ip: String(o.ip ?? "").trim(),
        port: num(o.port) || String(o.port ?? ""),
        raw: o,
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

/** For each peer id, router peers that send_to this id (reverse of send_to). */
export function buildRecvFromMap(peers: RouterPeer[]): Map<number, number[]> {
  const m = new Map<number, number[]>();
  for (const p of peers) {
    for (const tid of p.send_to) {
      let arr = m.get(tid);
      if (!arr) {
        arr = [];
        m.set(tid, arr);
      }
      arr.push(p.id);
    }
  }
  for (const arr of m.values()) {
    arr.sort((a, b) => a - b);
  }
  return m;
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

/**
 * Label from nested `peer` JSON (`peer_status()`): `remote.hostname` may be the
 * literal `"null"` when no sockaddr is set yet (see `network_address_t::hostname()`).
 */
export function rtpRemoteLabel(obj: Record<string, unknown>): string {
  const r = obj.remote as Record<string, unknown> | undefined;
  if (!r) return "remote";
  const name = String(r.name ?? "").trim();
  let host = String(r.hostname ?? "").trim();
  if (host === "null") host = "";
  const pr = r.port;
  let port = "";
  if (pr !== undefined && pr !== null && pr !== "") {
    port = String(pr);
    if (port === "null") port = "";
  }
  if (name && host) return port ? `${name} @ ${host}:${port}` : `${name} @ ${host}`;
  if (host) return port ? `${host}:${port}` : host;
  return name || "remote";
}

/** Summary line for RTP client / RTP peer router rows (prefers configured connect_*). */
export function rtpClientConnectionSummary(peerRow: Record<string, unknown>): string {
  const ch = String(peerRow.connect_hostname ?? "").trim();
  const cpRaw = peerRow.connect_port;
  const cp =
    cpRaw !== undefined && cpRaw !== null && String(cpRaw).length > 0
      ? String(cpRaw)
      : "";
  if (ch || cp) {
    if (ch && cp) return `${ch}:${cp}`;
    return ch || cp;
  }
  const peerObj = peerRow.peer as Record<string, unknown> | undefined;
  return peerObj ? rtpRemoteLabel(peerObj) : "—";
}

export type ConnectionKind = "router" | "rtp_server" | "rtp_link";

export type ConnectionParticipant = {
  id: number;
  name: string;
  /** Side not present as a router peer right now (saved pair only). */
  unavailable?: boolean;
};

export type ConnectionRow = {
  id: string;
  kind: ConnectionKind;
  kindLabel: string;
  summary: string;
  /** Display direction: → one-way, ↔ merged A↔B router pair, · hub, → RTP link. */
  direction: string;
  /** Router peers that can be focused from the table (click). */
  participantPeers: ConnectionParticipant[];
  /** Extra non-router text (e.g. RTP remote labels). */
  participantNote?: string;
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
  /** Sum of packets_recv on listed router peers (activity ←). */
  recvSum: number;
  /** Sum of packets_sent on listed router peers (activity →). */
  sentSum: number;
  /** Present when this row merges A→B and B→A. */
  bidirectional?: boolean;
  /** Stored in the connection database. */
  persisted?: boolean;
  /** Show remove-from-database control. */
  canRemoveFromDb?: boolean;
  persistedSideA?: string;
  persistedSideB?: string;
};

function pairKeyUnordered(a: number, b: number): string {
  return a < b ? `${a}:${b}` : `${b}:${a}`;
}

export function buildConnections(peers: RouterPeer[]): ConnectionRow[] {
  const byId = new Map(peers.map((p) => [p.id, p]));
  const out: ConnectionRow[] = [];

  const peerLabel = (pr: RouterPeer): string => pr.name.trim() || `#${pr.id}`;

  const pushRouterRow = (
    parts: RouterPeer[],
    opts: {
      id: string;
      bidirectional: boolean;
      summary: string;
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
    /** Sum of packets_sent only — avoids double-counting the same routed packet (sent + recv on the wire). */
    const traffic = parts.reduce((s, x) => s + x.sent, 0);
    const recvSum = parts.reduce((s, x) => s + x.recv, 0);
    const sentSum = parts.reduce((s, x) => s + x.sent, 0);
    const direction = opts.bidirectional ? "↔" : "→";
    const participantPeers: ConnectionParticipant[] = parts.map((x) => ({
      id: x.id,
      name: x.name || "—",
    }));
    out.push({
      id: opts.id,
      kind: "router",
      kindLabel: opts.bidirectional ? "Router bidi" : "Router",
      summary: opts.summary,
      direction,
      participantPeers,
      participantRouterIds: opts.participantRouterIds,
      nParticipants: opts.nParticipants,
      intUntilMax: intU.max,
      intSendMax: intS.max,
      rtpLastMax: rl.max,
      rtpAvgMax: ra.max,
      trafficTotal: traffic,
      recvSum,
      sentSum,
      bidirectional: opts.bidirectional || undefined,
    });
  };

  /** Every directed router edge `fromId->toId` (must be collected before emitting rows). */
  const directedEdges = new Set<string>();
  for (const p of peers) {
    for (const tid of p.send_to) {
      if (p.id !== tid) directedEdges.add(`${p.id}->${tid}`);
    }
  }

  const emittedBidirectionalPair = new Set<string>();
  const emittedDirectedOneway = new Set<string>();

  for (const p of peers) {
    for (const tid of p.send_to) {
      const from = p.id;
      if (from === tid) continue;

      const fwd = `${from}->${tid}`;
      const back = `${tid}->${from}`;
      const ukey = pairKeyUnordered(from, tid);

      if (directedEdges.has(fwd) && directedEdges.has(back)) {
        if (!emittedBidirectionalPair.has(ukey)) {
          emittedBidirectionalPair.add(ukey);
          const lo = Math.min(from, tid);
          const hi = Math.max(from, tid);
          const pLo = byId.get(lo)!;
          const pHi = byId.get(hi)!;
          pushRouterRow([pLo, pHi], {
            id: `route_pair:${lo}:${hi}`,
            bidirectional: true,
            summary: `${peerLabel(pLo)} ↔ ${peerLabel(pHi)}`,
            participantRouterIds: [lo, hi],
            nParticipants: 2,
          });
        }
        continue;
      }

      if (emittedDirectedOneway.has(fwd)) continue;
      emittedDirectedOneway.add(fwd);

      const toPeer = byId.get(tid);
      const parts = toPeer ? [p, toPeer] : [p];
      pushRouterRow(parts, {
        id: `route:${from}->${tid}`,
        bidirectional: false,
        summary: toPeer
          ? `${peerLabel(p)} → ${peerLabel(toPeer)}`
          : `${peerLabel(p)} → #${tid}`,
        participantRouterIds: toPeer ? [from, tid] : [from, tid],
        nParticipants: toPeer ? 2 : 1,
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
      direction: "·",
      participantPeers: [{ id: p.id, name: hubName || "—" }],
      participantNote: subPart ? `(hub)${subPart}` : "(hub)",
      participantRouterIds: [p.id],
      nParticipants: 1 + subLabels.length,
      intUntilMax: intU.max,
      intSendMax: intS.max,
      rtpLastMax: rl.max,
      rtpAvgMax: ra.max,
      trafficTotal: p.recv + p.sent,
      recvSum: p.recv,
      sentSum: p.sent,
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
    const rem = rtpClientConnectionSummary(p.raw);
    const kindLabel =
      p.type === "network_rtpmidi_client_t" ? "RTP client" : "RTP peer";
    out.push({
      id: `rtp_link:${p.id}`,
      kind: "rtp_link",
      kindLabel,
      summary: rem,
      direction: "→",
      participantPeers: [{ id: p.id, name: p.name || "—" }],
      participantNote: ` · ${rem}`,
      participantRouterIds: [p.id],
      nParticipants: 2,
      intUntilMax: intU.max,
      intSendMax: intS.max,
      rtpLastMax: rl.max,
      rtpAvgMax: ra.max,
      trafficTotal: p.recv + p.sent,
      recvSum: p.recv,
      sentSum: p.sent,
    });
  }

  return out;
}

/**
 * After connect adds a local_alsa_listener_t peer, find the Connections row that
 * lists that router peer. If multiple listeners share the display name, the
 * greatest peer id (typically the newest) is chosen.
 */
export function connectionRowIdForAlsaListenerName(
  peers: RouterPeer[],
  serviceName: string,
): string | null {
  const name = serviceName.trim();
  const listeners = peers.filter(
    (p) =>
      p.type === "local_alsa_listener_t" &&
      (p.name === name || p.name.trim() === name),
  );
  if (!listeners.length) return null;
  const peer = listeners.reduce((a, b) => (a.id > b.id ? a : b));
  const rows = buildConnections(peers);
  const row = rows.find((r) => r.participantRouterIds.includes(peer.id));
  return row?.id ?? null;
}

/** Connections row that lists both router peers (e.g. local ALSA + RTP client). */
export function connectionRowIdLinkingPeers(
  peers: RouterPeer[],
  peerIdA: number,
  peerIdB: number,
): string | null {
  const rows = buildConnections(peers);
  const row = rows.find(
    (r) =>
      r.participantRouterIds.includes(peerIdA) &&
      r.participantRouterIds.includes(peerIdB),
  );
  return row?.id ?? null;
}
