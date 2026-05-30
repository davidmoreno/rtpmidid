/** Merge live endpoints (PeersCards) with registry devices (devices.list). */

import type { Endpoint } from "./endpoints";
import type { MidiAlsaSeqEntry } from "./midiEnumerate";
import type { RouterPeer } from "./model";
import {
  canonicalIdentity,
  formatIdentityLabel,
  identitiesEqual,
  identityFromPeerRow,
  parseIdentity,
} from "./deviceIdentity";
import { sourceLabel, type RegistryDevice } from "./devicesList";
import { groupForEndpoint, type EndpointGroup } from "./endpointPickerUtils";

export type DeviceStatusTag = "online" | "offline";

export type MergedDeviceRow = {
  /** Card id for refs, favorites, highlight (device identity or `registry:…`). */
  id: string;
  endpoint: Endpoint | null;
  registry: RegistryDevice | null;
  label: string;
  sub: string;
  peerId?: number;
  online: boolean;
  /** Human source tag; null when registry DB is disabled. */
  sourceTag: string | null;
  statusTag: DeviceStatusTag;
  lastSeen?: number;
  /** Registry row with no live endpoint / peer. */
  isOfflineOnly: boolean;
  /** endpoint.connect / monitor.start identity (live endpoint or registry). */
  connectIdentity: string | null;
  /** Used for sort/filter (local vs remote, activity, …). */
  sortEndpoint: Endpoint;
};

export const REGISTRY_CARD_PREFIX = "registry:";

export function registryCardId(identity: string): string {
  return `${REGISTRY_CARD_PREFIX}${identity}`;
}

function groupForRegistryType(typePrefix: string): EndpointGroup {
  return typePrefix === "rtpmidi_client" ? "remote" : "local";
}

function buildRegistryIndexes(devices: RegistryDevice[]) {
  const byPeerId = new Map<number, RegistryDevice>();
  const byIdentity = new Map<string, RegistryDevice>();
  for (const d of devices) {
    byIdentity.set(canonicalIdentity(d.identity), d);
    if (d.peerId !== undefined) byPeerId.set(d.peerId, d);
  }
  return { byPeerId, byIdentity };
}

function findRegistryForEndpoint(
  endpoint: Endpoint,
  peer: RouterPeer | undefined,
  byPeerId: Map<number, RegistryDevice>,
  byIdentity: Map<string, RegistryDevice>,
): RegistryDevice | null {
  if (endpoint.peerId !== undefined) {
    const hit = byPeerId.get(endpoint.peerId);
    if (hit) return hit;
  }
  const hit = byIdentity.get(canonicalIdentity(endpoint.identity));
  if (hit) return hit;
  if (peer) {
    const pid = identityFromPeerRow(peer);
    if (pid) {
      const byPeer = byIdentity.get(canonicalIdentity(pid));
      if (byPeer) return byPeer;
    }
  }
  for (const d of byIdentity.values()) {
    if (identitiesEqual(d.identity, endpoint.identity)) return d;
  }
  return null;
}

function sourceTagFor(
  registry: RegistryDevice | null,
  registryEnabled: boolean,
): string | null {
  if (!registryEnabled) return null;
  if (registry) return sourceLabel(registry.source);
  return "Session";
}

function rowFromEndpoint(
  endpoint: Endpoint,
  registry: RegistryDevice | null,
  registryEnabled: boolean,
): MergedDeviceRow {
  const online = endpoint.peerId !== undefined || endpoint.kind === "rtpmidi";
  return {
    id: endpoint.identity,
    endpoint,
    registry,
    label: registry?.name || endpoint.label,
    sub: endpoint.sub,
    peerId: endpoint.peerId ?? registry?.peerId,
    online,
    sourceTag: sourceTagFor(registry, registryEnabled),
    statusTag: online ? "online" : "offline",
    lastSeen: registry?.lastSeen,
    isOfflineOnly: false,
    connectIdentity: endpoint.identity,
    sortEndpoint: endpoint,
  };
}

function rowFromRegistry(
  registry: RegistryDevice,
  registryEnabled: boolean,
): MergedDeviceRow {
  const label = registry.name || formatIdentityLabel(registry.identity);
  const parsed = parseIdentity(registry.identity);
  const kind =
    registry.type === "rtpmidi_client" || parsed?.typePrefix === "rtpmidi_client"
      ? "rtpmidi"
      : "peer";
  const sortEndpoint: Endpoint = {
    identity: registry.identity,
    kind,
    label,
    sub: formatIdentityLabel(registry.identity),
  };
  return {
    id: registryCardId(registry.identity),
    endpoint: null,
    registry,
    label,
    sub: registry.identity,
    peerId: registry.peerId,
    online: false,
    sourceTag: sourceTagFor(registry, registryEnabled),
    statusTag: "offline",
    lastSeen: registry.lastSeen,
    isOfflineOnly: true,
    connectIdentity: registry.identity,
    sortEndpoint,
  };
}

export function mergeDeviceList(args: {
  endpoints: Endpoint[];
  registryDevices: RegistryDevice[];
  registryEnabled: boolean;
  peers: RouterPeer[];
  alsaSeq: MidiAlsaSeqEntry[];
}): MergedDeviceRow[] {
  const { endpoints, registryDevices, registryEnabled, peers } = args;
  const byPeerIdPeer = new Map(peers.map((p) => [p.id, p]));
  const { byPeerId, byIdentity } = buildRegistryIndexes(registryDevices);
  const matchedRegistry = new Set<string>();
  const out: MergedDeviceRow[] = [];

  for (const endpoint of endpoints) {
    const peer =
      endpoint.peerId !== undefined
        ? byPeerIdPeer.get(endpoint.peerId)
        : undefined;
    const registry = registryEnabled
      ? findRegistryForEndpoint(endpoint, peer, byPeerId, byIdentity)
      : null;
    if (registry) matchedRegistry.add(registry.identity);
    out.push(rowFromEndpoint(endpoint, registry, registryEnabled));
  }

  if (registryEnabled) {
    for (const registry of registryDevices) {
      if (matchedRegistry.has(registry.identity)) continue;
      if (registry.online && registry.peerId !== undefined) {
        const liveEndpoint = endpoints.find((e) => e.peerId === registry.peerId);
        if (liveEndpoint) continue;
      }
      out.push(rowFromRegistry(registry, registryEnabled));
    }
  }

  return out;
}

export function groupForMergedRow(row: MergedDeviceRow): EndpointGroup {
  if (row.endpoint) return groupForEndpoint(row.endpoint);
  if (row.registry) return groupForRegistryType(row.registry.type);
  return "local";
}
