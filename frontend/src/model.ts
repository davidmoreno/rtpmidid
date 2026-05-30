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

export type ConnectionKind = "router";
/** High-level category shown as the Type badge in the Connections table. */
export type ConnectionType = "midirouter" | "alsaseq";

export type ConnectionParticipant = {
  id: number;
  name: string;
  /** Side not present as a router peer right now (saved pair only). */
  unavailable?: boolean;
};

/**
 * One side of a Connection row (used to render the dominant "from <-> to" cell
 * and to drive the Save/Remove DB column).
 *
 * `endpointId` is the same id the daemon understands for endpoint.connect /
 * endpoint.disconnect / monitor.start (e.g. `peer:5`, `alsa:128:0`,
 * `mdns:Name::5004`). It is also the card id used by DevicesTab so clicking a
 * side from the Connections page can scroll + highlight the matching device
 * card.
 *
 * `stableId` is the SQLite stable id (e.g. `alsa:<client_name>:<port_name>`).
 * It is filled when the side could be resolved at row-build time so the
 * Connections page can match against persisted pairs and offer the "+" / "-"
 * DB button without a server round-trip.
 */
export type ConnectionEndpointRef = {
  endpointId: string;
  label: string;
  /** Router peer id when this side is a materialised router peer. */
  peerId?: number;
  /** Stable id for SQLite persistence; absent when the side has no stable identity. */
  stableId?: string;
  /** True when this side is referenced but not currently materialised (saved-only). */
  unavailable?: boolean;
};

export type ConnectionRow = {
  id: string;
  kind: ConnectionKind;
  /** Top-level Type badge shown in the table. */
  type: ConnectionType;
  kindLabel: string;
  summary: string;
  /** Display direction: → one-way, ↔ merged A↔B router pair, · hub, → RTP link. */
  direction: string;
  /** Dominant "from -> to" cell sides. */
  from: ConnectionEndpointRef;
  to: ConnectionEndpointRef;
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
  /** False when saved connection is disabled in DB. */
  persistedEnabled?: boolean;
  /** Show remove-from-database control. */
  canRemoveFromDb?: boolean;
  /** True when both sides are resolvable AND not already persisted. */
  canAddToDb?: boolean;
  /** Tooltip explaining why the row cannot be saved (when !persisted && !canAddToDb). */
  cannotSaveReason?: string;
  persistedSideA?: string;
  persistedSideB?: string;
};

/** Stable id helper for an ALSA-seq endpoint (`alsa:<client_name>:<port_name>`).
 *  Mirrors the daemon's `compute_stable_id` for `peer_device_alsa_seq_t` and the
 *  alsa stable id produced by `resolve_side_to_stable_id` in control_rpc.cpp.
 *  Returns undefined when either name is empty.
 */
export function alsaStableIdFromNames(
  clientName: string,
  portName: string,
): string | undefined {
  if (!clientName || !portName) return undefined;
  const esc = (s: string) => s.replace(/:/g, "|");
  return `alsa:${esc(clientName)}:${esc(portName)}`;
}

/** Compute the stable id for a router peer mirroring `compute_stable_id` in
 *  src/connection_db.cpp. Used to decide if a row is saveable without going
 *  through the daemon. Returns undefined only for peers with no stable
 *  identity at all (e.g. `webui_midi_monitor_peer_t`, or peers without a name
 *  and no structural identifier yet).
 */
export function peerStableId(peer: RouterPeer): string | undefined {
  const esc = (s: string) => s.replace(/:/g, "|");
  const make = (prefix: string, parts: string[]): string | undefined => {
    if (parts.some((p) => !p)) return undefined;
    return prefix + ":" + parts.map(esc).join(":");
  };
  const raw = peer.raw as Record<string, unknown>;
  const peerName = (peer.name || String(raw.name ?? "")).trim();
  /* `network_address_t::hostname()` formats unresolved sockaddrs as the
     literal string "null"; treat that as empty. */
  const realHost = (h: string) => (h && h !== "null" ? h : "");

  switch (peer.type) {
    case "peer_device_alsa_seq_t": {
      const asf = raw.alsa_subscribe_from as
        | { client_name?: string; port_name?: string }
        | undefined;
      if (asf && asf.client_name && asf.port_name) {
        const sid = make("alsa", [asf.client_name, asf.port_name]);
        if (sid) return sid;
      }
      if (peerName) return make("alsa_local", [peerName]);
      return undefined;
    }
    case "peer_device_rawmidi_t": {
      const dev = String(raw.device ?? "");
      if (dev) return make("rawmidi", [dev]);
      if (peerName) return make("rawmidi_named", [peerName]);
      return undefined;
    }
    case "peer_device_rtpmidi_client_t": {
      let hostname = realHost(String(raw.connect_hostname ?? "").trim());
      let svc = "";
      const p = raw.peer as Record<string, unknown> | undefined;
      if (!hostname && p) {
        const rem = p.remote as Record<string, unknown> | undefined;
        if (rem) hostname = realHost(String(rem.hostname ?? "").trim());
      }
      if (p) {
        const rem = p.remote as Record<string, unknown> | undefined;
        if (rem) svc = String(rem.name ?? "").trim();
      }
      if (!svc) svc = peerName;
      if (hostname && svc) return make("rtpmidi", [hostname, svc]);
      if (peerName) return make("rtpmidi_client_named", [peerName]);
      return undefined;
    }
    case "peer_device_rtpmidi_session_t": {
      const p = raw.peer as Record<string, unknown> | undefined;
      const rem = p
        ? (p.remote as Record<string, unknown> | undefined)
        : undefined;
      const h = realHost(String(rem?.hostname ?? "").trim());
      const n = String(rem?.name ?? "").trim();
      if (h && n) return make("rtpmidi_in", [h, n]);
      if (peerName) return make("rtpmidi_in_named", [peerName]);
      return undefined;
    }
    case "peer_export_rtpmidi_server_t": {
      if (peerName) return make("rtpmidi_server", [peerName]);
      return undefined;
    }
    case "peer_import_alsa_rtp_t": {
      if (!peerName) return undefined;
      const pos = peerName.indexOf(" <-> ");
      if (pos >= 0) {
        const remote = peerName.substring(pos + 5);
        if (remote) return make("alsa_listener", [remote]);
      }
      return make("alsa_listener_named", [peerName]);
    }
    case "peer_import_rtpmidi_t": {
      const listening = raw.listening as { name?: string } | undefined;
      const n =
        listening?.name && listening.name.length > 0
          ? listening.name
          : peerName;
      if (n) return make("rtpmidi_multi", [n]);
      return undefined;
    }
    case "peer_export_alsa_network_t": {
      if (peerName) return make("alsa_multi", [peerName]);
      return undefined;
    }
    /* Explicitly unsaveable: session-bound sink created by monitor.start with a
       uuid that changes every connection. */
    case "webui_midi_monitor_peer_t":
      return undefined;
    default: {
      /* Generic fallback: address by configured name with a short type-derived
         prefix so unknown peer types remain saveable. */
      if (!peer.type || !peerName) return undefined;
      let prefix = peer.type;
      if (prefix.endsWith("_t")) prefix = prefix.slice(0, -2);
      return make(prefix, [peerName]);
    }
  }
}

function pairKeyUnordered(a: number, b: number): string {
  return a < b ? `${a}:${b}` : `${b}:${a}`;
}

export function buildConnections(peers: RouterPeer[]): ConnectionRow[] {
  const byId = new Map(peers.map((p) => [p.id, p]));
  const out: ConnectionRow[] = [];

  const peerLabel = (pr: RouterPeer): string => pr.name.trim() || `#${pr.id}`;
  const peerRef = (pr: RouterPeer): ConnectionEndpointRef => ({
    endpointId: `peer:${pr.id}`,
    label: peerLabel(pr),
    peerId: pr.id,
    stableId: peerStableId(pr),
  });

  const pushRouterRow = (
    parts: RouterPeer[],
    opts: {
      id: string;
      bidirectional: boolean;
      summary: string;
      participantRouterIds: number[];
      nParticipants: number;
      from: ConnectionEndpointRef;
      to: ConnectionEndpointRef;
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
      type: "midirouter",
      kindLabel: opts.bidirectional ? "Router bidi" : "Router",
      summary: opts.summary,
      direction,
      from: opts.from,
      to: opts.to,
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
            from: peerRef(pLo),
            to: peerRef(pHi),
          });
        }
        continue;
      }

      if (emittedDirectedOneway.has(fwd)) continue;
      emittedDirectedOneway.add(fwd);

      const toPeer = byId.get(tid);
      const parts = toPeer ? [p, toPeer] : [p];
      const toRef: ConnectionEndpointRef = toPeer
        ? peerRef(toPeer)
        : {
            endpointId: `peer:${tid}`,
            label: `#${tid}`,
            peerId: tid,
            unavailable: true,
          };
      pushRouterRow(parts, {
        id: `route:${from}->${tid}`,
        bidirectional: false,
        summary: toPeer
          ? `${peerLabel(p)} → ${peerLabel(toPeer)}`
          : `${peerLabel(p)} → #${tid}`,
        participantRouterIds: toPeer ? [from, tid] : [from, tid],
        nParticipants: toPeer ? 2 : 1,
        from: peerRef(p),
        to: toRef,
      });
    }
  }

  /* Note: previous versions also emitted aggregate "rtp_server" and "rtp_link"
     rows here. Those describe a single peer's outgoing/incoming RTP state
     (e.g. "server X has remotes A, B, C") rather than a saveable router edge
     between two endpoints. They are already visualised per-card in the
     Devices tab, and their synthetic "to" side (the remote summary string)
     has no stable id so they always rendered with the disabled circle. We
     drop them here to keep the Connections table focused on actual edges. */

  return out;
}

export type AlsaSubscriptionRaw = {
  from_client: number;
  from_port: number;
  to_client: number;
  to_port: number;
  from_label?: string;
  to_label?: string;
  from_client_name?: string;
  from_port_name?: string;
  to_client_name?: string;
  to_port_name?: string;
};

export function parseAlsaSubscriptions(raw: unknown): AlsaSubscriptionRaw[] {
  if (!Array.isArray(raw)) return [];
  const out: AlsaSubscriptionRaw[] = [];
  for (const x of raw) {
    if (!x || typeof x !== "object") continue;
    const o = x as Record<string, unknown>;
    const fc = Number(o.from_client);
    const fp = Number(o.from_port);
    const tc = Number(o.to_client);
    const tp = Number(o.to_port);
    if (
      !Number.isFinite(fc) ||
      !Number.isFinite(fp) ||
      !Number.isFinite(tc) ||
      !Number.isFinite(tp)
    )
      continue;
    out.push({
      from_client: fc,
      from_port: fp,
      to_client: tc,
      to_port: tp,
      from_label: typeof o.from_label === "string" ? o.from_label : undefined,
      to_label: typeof o.to_label === "string" ? o.to_label : undefined,
      from_client_name:
        typeof o.from_client_name === "string" ? o.from_client_name : undefined,
      from_port_name:
        typeof o.from_port_name === "string" ? o.from_port_name : undefined,
      to_client_name:
        typeof o.to_client_name === "string" ? o.to_client_name : undefined,
      to_port_name:
        typeof o.to_port_name === "string" ? o.to_port_name : undefined,
    });
  }
  return out;
}

/**
 * Build Connection rows from ALSA-seq aconnect subscriptions.
 *
 * - Opposite directed pairs (A->B and B->A) are merged into one bidi row.
 * - Sides expose `endpointId = alsa:<c>:<p>` (matches Devices tab card ids and
 *   the daemon's `endpoint.connect` parsing) and `stableId =
 *   alsa:<client_name>:<port_name>` (matches `compute_stable_id` in
 *   `connection_db.cpp`, so saved-pair matching is purely client-side).
 * - `peerId` is set when the ALSA port also backs a router peer
 *   (`peer_device_alsa_seq_t.alsa_subscribe_from`); this lets the click-through
 *   land on the right Devices card even when a router peer wraps the port.
 */
export function buildAlsaSubscriptionConnections(
  subs: AlsaSubscriptionRaw[],
  peers: RouterPeer[],
): ConnectionRow[] {
  const peerByAlsa = new Map<string, RouterPeer>();
  for (const p of peers) {
    if (p.type !== "peer_device_alsa_seq_t") continue;
    const raw = p.raw as Record<string, unknown>;
    const asf = raw.alsa_subscribe_from as
      | { client?: unknown; port?: unknown }
      | undefined;
    if (!asf) continue;
    const c = Number(asf.client);
    const pt = Number(asf.port);
    if (!Number.isFinite(c) || !Number.isFinite(pt)) continue;
    peerByAlsa.set(`${c}:${pt}`, p);
  }

  const sideRef = (
    client: number,
    port: number,
    labelHint: string | undefined,
    clientName: string | undefined,
    portName: string | undefined,
  ): ConnectionEndpointRef => {
    const endpointId = `alsa:${client}:${port}`;
    const matchedPeer = peerByAlsa.get(`${client}:${port}`);
    const cn = clientName ?? "";
    const pn = portName ?? "";
    const label =
      cn && pn
        ? `${cn} · ${pn}`
        : labelHint
          ? labelHint
          : `${client}:${port}`;
    return {
      endpointId,
      label,
      peerId: matchedPeer?.id,
      stableId: alsaStableIdFromNames(cn, pn),
    };
  };

  /** Stable key independent of direction so A->B and B->A collapse. */
  const undirectedKey = (
    fc: number,
    fp: number,
    tc: number,
    tp: number,
  ): string => {
    const a = `${fc}:${fp}`;
    const b = `${tc}:${tp}`;
    return a < b ? `${a}|${b}` : `${b}|${a}`;
  };

  const directedKey = (
    fc: number,
    fp: number,
    tc: number,
    tp: number,
  ): string => `${fc}:${fp}->${tc}:${tp}`;

  const directedSet = new Set<string>();
  for (const s of subs) {
    directedSet.add(directedKey(s.from_client, s.from_port, s.to_client, s.to_port));
  }

  /* Cache (client, port) -> labels so two passes (sub list scan, side lookup)
     don't both walk the whole `subs` array per row. */
  type Names = { clientName?: string; portName?: string; labelHint?: string };
  const namesByAddr = new Map<string, Names>();
  for (const s of subs) {
    const fk = `${s.from_client}:${s.from_port}`;
    if (!namesByAddr.has(fk)) {
      namesByAddr.set(fk, {
        clientName: s.from_client_name,
        portName: s.from_port_name,
        labelHint: s.from_label,
      });
    }
    const tk = `${s.to_client}:${s.to_port}`;
    if (!namesByAddr.has(tk)) {
      namesByAddr.set(tk, {
        clientName: s.to_client_name,
        portName: s.to_port_name,
        labelHint: s.to_label,
      });
    }
  }
  const lookupNames = (client: number, port: number): Names =>
    namesByAddr.get(`${client}:${port}`) ?? {};

  /* Stable iteration order so the table doesn't reshuffle between polls. */
  const sorted = [...subs].sort((a, b) => {
    if (a.from_client !== b.from_client) return a.from_client - b.from_client;
    if (a.from_port !== b.from_port) return a.from_port - b.from_port;
    if (a.to_client !== b.to_client) return a.to_client - b.to_client;
    return a.to_port - b.to_port;
  });

  const seenUndirected = new Set<string>();
  const out: ConnectionRow[] = [];
  for (const s of sorted) {
    const undir = undirectedKey(
      s.from_client,
      s.from_port,
      s.to_client,
      s.to_port,
    );
    if (seenUndirected.has(undir)) continue;
    seenUndirected.add(undir);

    const reverseKey = directedKey(
      s.to_client,
      s.to_port,
      s.from_client,
      s.from_port,
    );
    const bidi = directedSet.has(reverseKey);

    /* For bidi rows, orient "low" address as `from` so the label is stable
       regardless of which direction the daemon listed first. For one-way rows
       keep the original direction so the arrow matches reality. */
    const fwdLow =
      `${s.from_client}:${s.from_port}` < `${s.to_client}:${s.to_port}`;
    const useFromOrigin = !bidi || fwdLow;
    const fromClient = useFromOrigin ? s.from_client : s.to_client;
    const fromPort = useFromOrigin ? s.from_port : s.to_port;
    const toClient = useFromOrigin ? s.to_client : s.from_client;
    const toPort = useFromOrigin ? s.to_port : s.from_port;

    const fromNames = lookupNames(fromClient, fromPort);
    const toNames = lookupNames(toClient, toPort);

    const fromSide = sideRef(
      fromClient,
      fromPort,
      fromNames.labelHint,
      fromNames.clientName,
      fromNames.portName,
    );
    const toSide = sideRef(
      toClient,
      toPort,
      toNames.labelHint,
      toNames.clientName,
      toNames.portName,
    );

    out.push({
      id: `alsa_sub:${undir}${bidi ? ":bidi" : ""}`,
      kind: "router",
      type: "alsaseq",
      kindLabel: bidi ? "ALSA bidi" : "ALSA",
      summary: `${fromSide.label} ${bidi ? "↔" : "→"} ${toSide.label}`,
      direction: bidi ? "↔" : "→",
      from: fromSide,
      to: toSide,
      participantPeers: [],
      participantRouterIds: [],
      nParticipants: 2,
      trafficTotal: 0,
      recvSum: 0,
      sentSum: 0,
      bidirectional: bidi || undefined,
    });
  }

  return out;
}

/**
 * After connect adds a peer_import_alsa_rtp_t peer, find the Connections row that
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
      p.type === "peer_import_alsa_rtp_t" &&
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
