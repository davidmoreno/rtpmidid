import type { MdnsRemote, RouterPeer } from "./model";
import { groupMdnsRemotes } from "./model";
import type { MidiAlsaSeqEntry, MidiRawmidiEntry } from "./midiEnumerate";

export type EndpointKind = "alsa_seq" | "rawmidi" | "rtpmidi";

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
  for (const p of peers) {
    if (p.type !== "local_alsa_peer_t") continue;
    const raw = peerRaw(p);
    const asf = raw.alsa_subscribe_from as { client?: unknown; port?: unknown } | undefined;
    if (!asf) continue;
    if (Number(asf.client) === e.client && Number(asf.port) === e.port) return peerId(p);
  }
  return undefined;
}

function matchPeerForRaw(peers: RouterPeer[], e: MidiRawmidiEntry): number | undefined {
  for (const p of peers) {
    if (p.type !== "local_rawmidi_peer_t") continue;
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
    if (p.type !== "network_rtpmidi_client_t") continue;
    const raw = peerRaw(p);
    const ch = String(raw.connect_hostname ?? "").trim();
    const cp = String(raw.connect_port ?? "").trim();
    if (!ch || !cp) continue;
    if (cp !== pstr) continue;
    if (hostCandidates.includes(ch)) return peerId(p);
  }
  return undefined;
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
  for (const g of groups) {
    const port = g.port;
    const cands = remoteHostCandidates(g.instances);
    // Display hostname when available, otherwise resolved IP.
    const hostnames = g.instances.map((r) => r.hostname.trim()).filter((x) => x);
    const ips = g.instances.map((r) => r.ip.trim()).filter((x) => x);
    const best = hostnames[0] || ips[0] || "";
    const hostSub = best ? `${best}:${String(port)}` : `${String(port)}`;
    out.push({
      id: endpointIdForMdns(g.name, port),
      kind: "rtpmidi",
      label: g.name || "Remote",
      sub: `mDNS · ${hostSub}`,
      peerId: matchPeerForRemote(args.peers, cands, port),
    });
  }

  // Stable sort: kind then label then id.
  const rank: Record<EndpointKind, number> = { rtpmidi: 0, alsa_seq: 1, rawmidi: 2 };
  out.sort((a, b) => {
    const ra = rank[a.kind];
    const rb = rank[b.kind];
    if (ra !== rb) return ra - rb;
    const c = a.label.localeCompare(b.label);
    return c !== 0 ? c : a.id.localeCompare(b.id);
  });

  return out;
}

