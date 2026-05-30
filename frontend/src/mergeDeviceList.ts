/** Merge live endpoints (PeersCards) with registry devices (devices.list). */

import type { Endpoint } from "./endpoints";
import type { MidiAlsaSeqEntry } from "./midiEnumerate";
import { peerStableId, type RouterPeer } from "./model";
import {
  endpointIdFromIdentity,
  formatIdentityLabel,
  parseIdentity,
  serializeIdentity,
} from "./deviceIdentity";
import { sourceLabel, type RegistryDevice } from "./devicesList";
import { groupForEndpoint, type EndpointGroup } from "./endpointPickerUtils";

export type DeviceStatusTag = "online" | "offline";

export type MergedDeviceRow = {
  /** Card id for refs, favorites, highlight (endpoint id or `registry:…`). */
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
  /** endpoint.connect / monitor.start id (live endpoint or derived from identity). */
  connectEndpointId: string | null;
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

function alsaIdentityForEndpoint(
  endpointId: string,
  alsaSeq: MidiAlsaSeqEntry[],
): string | undefined {
  if (!endpointId.startsWith("alsa:")) return undefined;
  const rest = endpointId.slice(5);
  const colon = rest.indexOf(":");
  if (colon < 0) return undefined;
  const client = Number(rest.slice(0, colon));
  const port = Number(rest.slice(colon + 1));
  if (!Number.isFinite(client) || !Number.isFinite(port)) return undefined;
  const row = alsaSeq.find((a) => a.client === client && a.port === port);
  if (!row?.client_name || !row?.port_name) return undefined;
  const parsed = parseIdentity(
    serializeIdentity({
      typePrefix: "alsa_seq",
      fields: [
        { key: "client", value: row.client_name, bracketed: false },
        { key: "port", value: row.port_name, bracketed: false },
      ],
    }),
  );
  return parsed ? serializeIdentity(parsed) : undefined;
}

function buildRegistryIndexes(devices: RegistryDevice[]) {
  const byPeerId = new Map<number, RegistryDevice>();
  const byIdentity = new Map<string, RegistryDevice>();
  for (const d of devices) {
    byIdentity.set(d.identity, d);
    if (d.peerId !== undefined) byPeerId.set(d.peerId, d);
  }
  return { byPeerId, byIdentity };
}

function findRegistryForEndpoint(
  endpoint: Endpoint,
  peer: RouterPeer | undefined,
  byPeerId: Map<number, RegistryDevice>,
  byIdentity: Map<string, RegistryDevice>,
  alsaSeq: MidiAlsaSeqEntry[],
): RegistryDevice | null {
  if (endpoint.peerId !== undefined) {
    const hit = byPeerId.get(endpoint.peerId);
    if (hit) return hit;
  }
  if (endpoint.id.startsWith("host:")) {
    for (const d of byIdentity.values()) {
      if (endpointIdFromIdentity(d.identity) === endpoint.id) return d;
    }
  }
  if (peer) {
    const legacy = peerStableId(peer);
    if (legacy) {
      const hit = byIdentity.get(legacy);
      if (hit) return hit;
    }
  }
  const alsaId = alsaIdentityForEndpoint(endpoint.id, alsaSeq);
  if (alsaId) {
    const hit = byIdentity.get(alsaId);
    if (hit) return hit;
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
    id: endpoint.id,
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
    connectEndpointId: endpoint.id,
    sortEndpoint: endpoint,
  };
}

function rowFromRegistry(
  registry: RegistryDevice,
  registryEnabled: boolean,
): MergedDeviceRow {
  const label = registry.name || formatIdentityLabel(registry.identity);
  const connectEndpointId = endpointIdFromIdentity(registry.identity);
  const sortEndpoint: Endpoint = {
    id: connectEndpointId ?? registryCardId(registry.identity),
    kind: registry.type === "rtpmidi_client" ? "rtpmidi" : "peer",
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
    connectEndpointId,
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
  const { endpoints, registryDevices, registryEnabled, peers, alsaSeq } = args;
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
      ? findRegistryForEndpoint(
          endpoint,
          peer,
          byPeerId,
          byIdentity,
          alsaSeq,
        )
      : null;
    if (registry) matchedRegistry.add(registry.identity);
    out.push(rowFromEndpoint(endpoint, registry, registryEnabled));
  }

  if (registryEnabled) {
    for (const registry of registryDevices) {
      if (matchedRegistry.has(registry.identity)) continue;
      if (registry.online && registry.peerId !== undefined) {
        const connectId = endpointIdFromIdentity(registry.identity);
        const liveEndpoint =
          connectId !== null
            ? endpoints.find((e) => e.id === connectId)
            : undefined;
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
