import type { RouterPeer } from "./model";
import type { Endpoint } from "./endpoints";

export type EndpointGroup = "local" | "remote";
export type EndpointSortKey = "activity" | "name" | "kind" | "connected";

export function groupForEndpoint(e: Endpoint): EndpointGroup {
  return e.kind === "rtpmidi" ? "remote" : "local";
}

function endpointActivity(
  e: Endpoint,
  byPeerId: Map<number, RouterPeer>,
): number {
  if (e.peerId === undefined) return -1;
  const p = byPeerId.get(e.peerId);
  return p ? p.recv + p.sent : -1;
}

/** Same ordering as the Devices card grid: favorites first, then current Sort mode. */
export function compareEndpointsForDevicesSort(
  a: Endpoint,
  b: Endpoint,
  sortKey: EndpointSortKey,
  favoriteIds: Set<string>,
  isPeerConnected: Map<number, boolean>,
  byPeerId: Map<number, RouterPeer>,
): number {
  const fa = favoriteIds.has(a.identity) ? 1 : 0;
  const fb = favoriteIds.has(b.identity) ? 1 : 0;
  if (fa !== fb) return fb - fa;

  const activity = (e: Endpoint) => endpointActivity(e, byPeerId);
  const connFlag = (e: Endpoint): number =>
    e.peerId !== undefined && (isPeerConnected.get(e.peerId) ?? false) ? 1 : 0;

  if (sortKey === "name") {
    const c = a.label.localeCompare(b.label);
    return c !== 0 ? c : a.identity.localeCompare(b.identity);
  }
  if (sortKey === "kind") {
    const ca = groupForEndpoint(a);
    const cb = groupForEndpoint(b);
    if (ca !== cb) return ca === "remote" ? -1 : 1;
    const c = a.kind.localeCompare(b.kind);
    return c !== 0 ? c : a.label.localeCompare(b.label);
  }
  if (sortKey === "connected") {
    const da = connFlag(a);
    const db = connFlag(b);
    if (da !== db) return db - da;
    return activity(b) - activity(a);
  }
  return activity(b) - activity(a) || a.label.localeCompare(b.label);
}
