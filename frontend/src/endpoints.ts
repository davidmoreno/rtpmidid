import type { MdnsRemote, RouterPeer } from "./model";
import { groupMdnsRemotes } from "./model";
import type { MidiAlsaSeqEntry, MidiRawmidiEntry } from "./midiEnumerate";

export type EndpointKind =
  | "alsa_seq"
  | "rawmidi"
  | "rtpmidi"
  | "monitor"
  | "peer";

export type Endpoint = {
  /** Opaque id understood by server endpoint.connect/disconnect. */
  id: string;
  kind: EndpointKind;
  label: string;
  sub: string;
  /** Matched router peer id when materialized. */
  peerId?: number;
};

export function endpointIdForAlsa(client: number, port: number): string {
  return `alsa:${client}:${port}`;
}

export function endpointIdForRaw(device: string): string {
  return `raw:${device}`;
}

export function endpointIdForMdns(name: string, port: number | string): string {
  return `mdns:${name}::${String(port)}`;
}

export function endpointIdForHost(hostname: string, port: number | string): string {
  return `host:${hostname}:${String(port)}`;
}

/** Matches daemon `parse_endpoint_id` (`peer:<id>`) for router.disconnect via endpoint.disconnect. */
export function endpointIdForPeer(peerId: number): string {
  return `peer:${peerId}`;
}

/**
 * Synthetic endpoint for `webui_midi_monitor_peer_t` — not a hardware listing; used so device
 * cards can show monitor tee edges in “connected to” (see endpointByPeerId in PeersCards).
 */
/**
 * Display + disconnect id for any router peer not represented by a device row
 * (see “connected to” on PeersCards).
 */
export function endpointFromRouterPeer(p: RouterPeer): Endpoint {
  if (p.type === "webui_midi_monitor_peer_t") return endpointForWebUiMonitorPeer(p);
  return {
    id: endpointIdForPeer(p.id),
    kind: "peer",
    label: (p.name || "").trim() || `Peer #${p.id}`,
    sub: p.type || "router peer",
    peerId: p.id,
  };
}

export function endpointForWebUiMonitorPeer(p: RouterPeer): Endpoint {
  const raw = p.raw as Record<string, unknown>;
  const uuid = typeof raw.monitor_uuid === "string" ? raw.monitor_uuid : "";
  const shortUuid = uuid.length >= 8 ? uuid.slice(0, 8) : uuid;
  let tgt: number | undefined;
  const mt = raw.monitor_target_peer_id;
  if (typeof mt === "number" && Number.isFinite(mt)) tgt = mt;
  else if (typeof mt === "string") {
    const n = Number(mt);
    if (Number.isFinite(n)) tgt = n;
  }
  const tgtBit =
    tgt !== undefined ? `tee → peer #${tgt}` : "tee";
  return {
    id: endpointIdForPeer(p.id),
    kind: "monitor",
    label: p.name.trim() || "Web MIDI monitor",
    sub:
      shortUuid !== ""
        ? `${tgtBit} · ${shortUuid}…`
        : `${tgtBit} · #${p.id}`,
    peerId: p.id,
  };
}

/** Numeric comparison for RTP-MIDI / mDNS ports (may be string or number in JSON). */
export function rtpPortsEqual(portA: number | string, portB: unknown): boolean {
  const na =
    typeof portA === "number"
      ? portA
      : Number(String(portA).trim());
  const nb =
    typeof portB === "number"
      ? portB
      : Number(String(portB ?? "").trim());
  return Number.isFinite(na) && Number.isFinite(nb) && na === nb;
}

/**
 * mDNS device rows that correspond to RTP listeners this daemon exports
 * (`peer_export_rtpmidi_server_t` / `peer_import_rtpmidi_t`).
 */
export function collectBridgeExportedEndpointIds(
  peers: RouterPeer[],
  mdnsRemotes: MdnsRemote[],
): Set<string> {
  const out = new Set<string>();
  const groups = groupMdnsRemotes(mdnsRemotes);
  for (const g of groups) {
    const id = endpointIdForMdns(g.name, g.port);
    const name = g.name.trim();
    for (const p of peers) {
      if (p.type === "peer_export_rtpmidi_server_t") {
        const raw = peerRaw(p);
        const pn = String(p.name ?? raw.name ?? "").trim();
        if (pn !== name) continue;
        if (rtpPortsEqual(g.port, raw.port)) {
          out.add(id);
          break;
        }
      } else if (p.type === "peer_import_rtpmidi_t") {
        const raw = peerRaw(p);
        const pn = String(p.name ?? raw.name ?? "").trim();
        if (pn !== name) continue;
        const lp = raw.listening as Record<string, unknown> | undefined;
        const cp = lp?.control_port;
        if (rtpPortsEqual(g.port, cp)) {
          out.add(id);
          break;
        }
      }
    }
  }
  return out;
}

function isNum(x: unknown): x is number {
  return typeof x === "number" && Number.isFinite(x);
}

function peerId(p: RouterPeer): number {
  return p.id;
}

function peerRaw(p: RouterPeer): Record<string, unknown> {
  return p.raw as Record<string, unknown>;
}

function matchPeerForAlsa(peers: RouterPeer[], e: MidiAlsaSeqEntry): number | undefined {
  const webName = `WEB:ALSA:${e.client}:${e.port}`;
  for (const p of peers) {
    if (p.type !== "peer_device_alsa_seq_t") continue;
    const raw = peerRaw(p);
    if (String(raw.name ?? p.name ?? "") === webName) return peerId(p);
    const asf = raw.alsa_subscribe_from as { client?: unknown; port?: unknown } | undefined;
    if (!asf) continue;
    if (Number(asf.client) === e.client && Number(asf.port) === e.port) return peerId(p);
  }
  return undefined;
}

function matchPeerForRaw(peers: RouterPeer[], e: MidiRawmidiEntry): number | undefined {
  for (const p of peers) {
    if (p.type !== "peer_device_rawmidi_t") continue;
    const raw = peerRaw(p);
    if (String(raw.device ?? "") === e.device) return peerId(p);
  }
  return undefined;
}

function matchPeerForRemote(
  peers: RouterPeer[],
  hostCandidates: string[],
  port: number | string,
): number | undefined {
  const pstr = String(port);
  for (const p of peers) {
    if (p.type !== "peer_device_rtpmidi_client_t") continue;
    const raw = peerRaw(p);
    const ch = String(raw.connect_hostname ?? "").trim();
    const cp = String(raw.connect_port ?? "").trim();
    if (!ch || !cp) continue;
    if (cp !== pstr) continue;
    if (hostCandidates.includes(ch)) return peerId(p);
  }
  return undefined;
}

function rtpClientHostPort(
  p: RouterPeer,
): { hostname: string; port: string } | undefined {
  if (p.type !== "peer_device_rtpmidi_client_t") return undefined;
  const raw = peerRaw(p);
  const hostname = String(raw.connect_hostname ?? "").trim();
  const port = String(raw.connect_port ?? "").trim();
  if (!hostname || !port) return undefined;
  return { hostname, port };
}

function labelForRtpClientPeer(
  p: RouterPeer,
  hostname: string,
  port: string,
): { label: string; sub: string } {
  const raw = peerRaw(p);
  const rem = raw.peer as { remote?: { name?: unknown } } | undefined;
  const remoteName =
    rem?.remote && typeof rem.remote.name === "string"
      ? rem.remote.name.trim()
      : "";
  let label = remoteName || (p.name || "").trim();
  if (label.startsWith("WEB · ")) label = hostname;
  return {
    label: label || hostname,
    sub: `direct · ${hostname}:${port}`,
  };
}

/** Prefer mDNS/DNS hostnames; fall back to resolved IPs. */
function remoteHostCandidates(remotes: MdnsRemote[]): string[] {
  const hs = remotes.map((r) => r.hostname.trim()).filter((x) => x);
  const ips = remotes.map((r) => r.ip.trim()).filter((x) => x);
  // Keep both: we prefer hostnames for connecting, but IPs help match
  // already-created peers that used IPs.
  return Array.from(new Set([...hs, ...ips]));
}

export function buildEndpoints(args: {
  alsaSeq: MidiAlsaSeqEntry[];
  rawmidi: MidiRawmidiEntry[];
  mdnsRemotes: MdnsRemote[];
  peers: RouterPeer[];
}): Endpoint[] {
  const out: Endpoint[] = [];

  for (const e of args.alsaSeq) {
    out.push({
      id: endpointIdForAlsa(e.client, e.port),
      kind: "alsa_seq",
      label: e.label || `${e.client_name}:${e.port_name}`,
      sub: `${e.client}:${e.port} · ${e.kind || "alsa_seq"}`,
      peerId: matchPeerForAlsa(args.peers, e),
    });
  }

  for (const e of args.rawmidi) {
    out.push({
      id: endpointIdForRaw(e.device),
      kind: "rawmidi",
      label: e.label || e.device,
      sub: `${e.device} · ${e.kind || "rawmidi"}`,
      peerId: matchPeerForRaw(args.peers, e),
    });
  }

  const groups = groupMdnsRemotes(args.mdnsRemotes);
  const hostEndpointPeerIds = new Set<number>();
  for (const g of groups) {
    const port = g.port;
    const cands = remoteHostCandidates(g.instances);
    // Display hostname when available, otherwise resolved IP.
    const hostnames = g.instances.map((r) => r.hostname.trim()).filter((x) => x);
    const ips = g.instances.map((r) => r.ip.trim()).filter((x) => x);
    const best = hostnames[0] || ips[0] || "";
    const hostSub = best ? `${best}:${String(port)}` : `${String(port)}`;
    const matchedPeer = matchPeerForRemote(args.peers, cands, port);
    if (matchedPeer !== undefined) hostEndpointPeerIds.add(matchedPeer);
    out.push({
      id: endpointIdForMdns(g.name, port),
      kind: "rtpmidi",
      label: g.name || "Remote",
      sub: `mDNS · ${hostSub}`,
      peerId: matchedPeer,
    });
  }

  const seenHostEndpointIds = new Set<string>();
  for (const p of args.peers) {
    const hp = rtpClientHostPort(p);
    if (!hp) continue;
    const id = endpointIdForHost(hp.hostname, hp.port);
    if (seenHostEndpointIds.has(id)) continue;
    seenHostEndpointIds.add(id);
    if (hostEndpointPeerIds.has(p.id)) continue;
    const { label, sub } = labelForRtpClientPeer(p, hp.hostname, hp.port);
    out.push({
      id,
      kind: "rtpmidi",
      label,
      sub,
      peerId: p.id,
    });
  }

  // Stable sort: kind then label then id.
  const rank: Record<EndpointKind, number> = {
    rtpmidi: 0,
    alsa_seq: 1,
    rawmidi: 2,
    monitor: 3,
    peer: 4,
  };
  out.sort((a, b) => {
    const ra = rank[a.kind];
    const rb = rank[b.kind];
    if (ra !== rb) return ra - rb;
    const c = a.label.localeCompare(b.label);
    return c !== 0 ? c : a.id.localeCompare(b.id);
  });

  return out;
}

