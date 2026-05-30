import {
  useCallback,
  useEffect,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
} from "preact/hooks";
import type { MdnsRemote, RouterPeer } from "../model";
import {
  loadDeviceFavoriteIds,
  saveDeviceFavoriteIds,
} from "../deviceFavorites";
import {
  loadDeviceHiddenIds,
  loadShowHiddenDevices,
  saveDeviceHiddenIds,
  saveShowHiddenDevices,
} from "../deviceHidden";
import { buildRecvFromMap } from "../model";
import type { MidiAlsaSeqEntry, MidiRawmidiEntry } from "../midiEnumerate";
import type { RpcClient } from "../rpc";
import type { Endpoint } from "../endpoints";
import {
  buildEndpoints,
  collectBridgeExportedEndpointIds,
  endpointFromRouterPeer,
  endpointIdForPeer,
} from "../endpoints";
import {
  compareEndpointsForDevicesSort,
  groupForEndpoint,
  type EndpointGroup,
  type EndpointSortKey,
} from "../endpointPickerUtils";
import { DeviceMetaTags } from "./DeviceMetaTags";
import { ManualDeviceDialog } from "./ManualDeviceDialog";
import {
  EndpointPickerDialog,
  EndpointSelectBadge,
} from "./EndpointPickerDialog";
import { MidiMonitorModal } from "./MidiMonitorModal";
import { PeerLatencyHoverCell, peerCombinedLatencyMs } from "./LatencyBar";
import {
  groupForMergedRow,
  mergeDeviceList,
  type MergedDeviceRow,
} from "../mergeDeviceList";
import type { RegistryDevice } from "../devicesList";
import { CONFIRM_SKIP_HINT, runWithConfirm } from "../confirmAction";

type SortKey = EndpointSortKey;

function IconEye({ class: className = "h-4 w-4" }: { class?: string }) {
  return (
    <svg
      class={className}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      stroke-width="2"
      stroke-linecap="round"
      stroke-linejoin="round"
      aria-hidden
    >
      <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z" />
      <circle cx="12" cy="12" r="3" />
    </svg>
  );
}

function IconEyeOff({ class: className = "h-4 w-4" }: { class?: string }) {
  return (
    <svg
      class={className}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      stroke-width="2"
      stroke-linecap="round"
      stroke-linejoin="round"
      aria-hidden
    >
      <path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24" />
      <line x1="1" y1="1" x2="23" y2="23" />
    </svg>
  );
}

/** Router neighbour on a device card: prefer Devices-tab endpoint row, else router peer row. */
function resolveNeighborEndpoint(
  otherId: number,
  endpointByPeerId: Map<number, Endpoint>,
  byPeerId: Map<number, RouterPeer>,
): Endpoint {
  const ep = endpointByPeerId.get(otherId);
  if (ep) return ep;
  const rp = byPeerId.get(otherId);
  if (rp) return endpointFromRouterPeer(rp);
  return {
    id: endpointIdForPeer(otherId),
    kind: "peer",
    label: `Peer #${otherId}`,
    sub: "missing from router status",
    peerId: otherId,
  };
}

function matchesMergedQuery(row: MergedDeviceRow, q: string): boolean {
  const query = q.trim().toLowerCase();
  if (!query) return true;
  const hay = [
    row.id,
    row.label,
    row.sub,
    row.sourceTag ?? "",
    row.registry?.identity ?? "",
    row.registry?.type ?? "",
    row.sortEndpoint.kind,
  ]
    .join(" ")
    .toLowerCase();
  return hay.includes(query);
}

function AccentPill({
  group,
  connected,
  children,
}: {
  group: EndpointGroup;
  connected: boolean;
  children: string;
}) {
  const base =
    "inline-flex items-center gap-1 rounded border-2 px-1.5 py-0.5 font-mono text-[10px] font-black uppercase tracking-wide";
  const cls =
    group === "local"
      ? `${base} ui-pill-local ${connected ? "ui-pill-local-connected" : ""}`
      : `${base} ui-pill-remote ${connected ? "ui-pill-remote-connected" : ""}`;
  return <span class={cls}>{children}</span>;
}

function Led({
  label,
  active,
  group,
  disabled,
}: {
  label: "IN" | "OUT";
  active: boolean;
  group: EndpointGroup;
  disabled: boolean;
}) {
  const lit =
    group === "local" ? "ui-led-local-on" : "ui-led-remote-on";
  const unlit = "ui-led-off";
  return (
    <span
      title={
        disabled
          ? "Not materialized as a router peer yet"
          : label === "IN"
            ? "Inbound: packets_recv increased since previous poll"
            : "Outbound: packets_sent increased since previous poll"
      }
      class={`flex h-7 w-10 shrink-0 items-center justify-center border-2 font-mono text-[10px] font-black leading-none ease-out transition-[background-color,border-color,color,box-shadow,opacity,filter] duration-[500ms] ${
        disabled ? `${unlit} opacity-60 saturate-0` : active ? lit : unlit
      }`}
    >
      {label}
    </span>
  );
}

type Props = {
  peers: RouterPeer[];
  mdnsRemotes: MdnsRemote[];
  alsaSeq: MidiAlsaSeqEntry[];
  rawmidi: MidiRawmidiEntry[];
  alsaSubs: unknown[];
  registryDevices?: RegistryDevice[];
  registryEnabled?: boolean;
  /** When set, scroll to the matching card and apply the highlight ring. */
  highlightEndpointId?: string | null;
  rpc: RpcClient;
  onAfterAction: () => Promise<void> | void;
  onStatus: (msg: string) => void;
};

export function PeersCards({
  peers,
  mdnsRemotes,
  alsaSeq,
  rawmidi,
  alsaSubs,
  registryDevices = [],
  registryEnabled = false,
  highlightEndpointId,
  rpc,
  onAfterAction,
  onStatus,
}: Props) {
  const [showLocal, setShowLocal] = useState(true);
  const [showRemote, setShowRemote] = useState(true);
  const [connectedOnly, setConnectedOnly] = useState(false);
  const [query, setQuery] = useState("");
  const [sortKey, setSortKey] = useState<SortKey>("activity");
  const [showAddDevice, setShowAddDevice] = useState(false);
  const [editDevice, setEditDevice] = useState<RegistryDevice | null>(null);

  const endpoints = useMemo(
    () =>
      buildEndpoints({
        alsaSeq,
        rawmidi,
        mdnsRemotes,
        peers,
      }),
    [alsaSeq, rawmidi, mdnsRemotes, peers],
  );

  const autoHiddenIds = useMemo(
    () => collectBridgeExportedEndpointIds(peers, mdnsRemotes),
    [peers, mdnsRemotes],
  );

  const recvFrom = useMemo(() => buildRecvFromMap(peers), [peers]);
  const byPeerId = useMemo(() => new Map(peers.map((p) => [p.id, p])), [peers]);

  const isPeerConnected = useMemo(() => {
    const m = new Map<number, boolean>();
    for (const p of peers) {
      const inN = recvFrom.get(p.id)?.length ?? 0;
      m.set(p.id, p.send_to.length > 0 || inN > 0);
    }
    return m;
  }, [peers, recvFrom]);

  const endpointByPeerId = useMemo(() => {
    const m = new Map<number, Endpoint>();
    for (const e of endpoints) {
      if (e.peerId !== undefined) m.set(e.peerId, e);
    }
    return m;
  }, [endpoints]);

  const prevRef = useRef<Map<number, { recv: number; sent: number }> | null>(null);
  const [inPulse, setInPulse] = useState<Set<number>>(() => new Set());
  const [outPulse, setOutPulse] = useState<Set<number>>(() => new Set());

  useEffect(() => {
    const next = new Map<number, { recv: number; sent: number }>();
    for (const p of peers) next.set(p.id, { recv: p.recv, sent: p.sent });
    const prev = prevRef.current;
    prevRef.current = next;
    if (!prev) return;
    const npIn = new Set<number>();
    const npOut = new Set<number>();
    for (const p of peers) {
      const was = prev.get(p.id);
      if (!was) continue;
      if (p.recv > was.recv) npIn.add(p.id);
      if (p.sent > was.sent) npOut.add(p.id);
    }
    setInPulse(npIn);
    setOutPulse(npOut);
  }, [peers]);

  const cardRefs = useRef<Map<string, HTMLDivElement>>(new Map());
  const pendingFavoriteScrollRef = useRef<string | null>(null);
  const [highlightIds, setHighlightIds] = useState<Set<string>>(() => new Set());
  const [spotlightIds, setSpotlightIds] = useState<Set<string>>(() => new Set());
  const highlightTimer = useRef<number | undefined>(undefined);

  useEffect(
    () => () => {
      if (highlightTimer.current !== undefined)
        window.clearTimeout(highlightTimer.current);
    },
    [],
  );

  const pulseEndpoints = (ids: string[], scrollTo?: string) => {
    setHighlightIds(new Set(ids));
    if (highlightTimer.current !== undefined)
      window.clearTimeout(highlightTimer.current);
    highlightTimer.current = window.setTimeout(() => {
      setHighlightIds(new Set());
      highlightTimer.current = undefined;
    }, 1000);
    if (scrollTo) {
      // After actions, the list may re-sort; scroll after the DOM updates.
      const scroll = () => {
        const el = cardRefs.current.get(scrollTo);
        el?.scrollIntoView({ block: "nearest", behavior: "smooth" });
      };
      requestAnimationFrame(() => requestAnimationFrame(scroll));
    }
  };

  /* When the parent sets highlightEndpointId (e.g. user clicked a chip on the
     Connections page), reuse the favouriting spotlight effect: same 200ms
     scroll delay + 1s visible burst (background+shadow change is what makes it
     pop). Cleared automatically; the parent's setTimeout is purely a debounce
     guard so consecutive clicks don't queue up. */
  const externalSpotlightTimers = useRef<{
    scroll?: number;
    clear?: number;
  }>({});
  useEffect(() => {
    if (!highlightEndpointId) return;
    /* Same visual treatment as toggling a favourite: setSpotlightIds is the
       background+shadow swap that really makes the card pop (just adding the
       `ui-tr-highlight` ring is too subtle to notice when the tab changes). */
    setSpotlightIds(new Set([highlightEndpointId]));
    const t = externalSpotlightTimers.current;
    if (t.scroll !== undefined) window.clearTimeout(t.scroll);
    if (t.clear !== undefined) window.clearTimeout(t.clear);
    t.scroll = window.setTimeout(() => {
      const el = cardRefs.current.get(highlightEndpointId);
      el?.scrollIntoView({ block: "nearest", behavior: "smooth" });
    }, 200);
    t.clear = window.setTimeout(() => {
      setSpotlightIds(new Set());
    }, 200 + 1000);
    return () => {
      if (t.scroll !== undefined) window.clearTimeout(t.scroll);
      if (t.clear !== undefined) window.clearTimeout(t.clear);
    };
  }, [highlightEndpointId]);

  const [favoriteIds, setFavoriteIds] = useState<Set<string>>(() =>
    loadDeviceFavoriteIds(),
  );

  const toggleFavorite = useCallback((id: string) => {
    pendingFavoriteScrollRef.current = id;
    setFavoriteIds((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      saveDeviceFavoriteIds(next);
      return next;
    });
  }, []);

  const [hiddenIds, setHiddenIds] = useState<Set<string>>(() =>
    loadDeviceHiddenIds(),
  );
  const [showHidden, setShowHidden] = useState<boolean>(() =>
    loadShowHiddenDevices(),
  );

  const toggleManualHidden = useCallback((id: string) => {
    setHiddenIds((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      saveDeviceHiddenIds(next);
      return next;
    });
  }, []);

  const setShowHiddenPersist = useCallback((v: boolean) => {
    setShowHidden(v);
    saveShowHiddenDevices(v);
  }, []);

  /* layoutEffect: apply spotlight in the same frame as DOM reorder so paint always shows it */
  useLayoutEffect(() => {
    const id = pendingFavoriteScrollRef.current;
    if (id === null) return;
    pendingFavoriteScrollRef.current = null;

    setSpotlightIds(new Set([id]));

    const scrollTimer = window.setTimeout(() => {
      const el = cardRefs.current.get(id);
      el?.scrollIntoView({ block: "nearest", behavior: "smooth" });
    }, 200);

    const clearTimer = window.setTimeout(() => {
      setSpotlightIds(new Set());
    }, 200 + 1000);

    return () => {
      window.clearTimeout(scrollTimer);
      window.clearTimeout(clearTimer);
    };
  }, [favoriteIds]);

  const subsRows = useMemo(() => {
    const out: {
      from_client: number;
      from_port: number;
      to_client: number;
      to_port: number;
      from_label?: string;
      to_label?: string;
    }[] = [];
    for (const x of alsaSubs ?? []) {
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
      });
    }
    out.sort((a, b) => {
      if (a.from_client !== b.from_client) return a.from_client - b.from_client;
      if (a.from_port !== b.from_port) return a.from_port - b.from_port;
      if (a.to_client !== b.to_client) return a.to_client - b.to_client;
      return a.to_port - b.to_port;
    });
    return out;
  }, [alsaSubs]);

  const mergedDevices = useMemo(
    () =>
      mergeDeviceList({
        endpoints,
        registryDevices,
        registryEnabled,
        peers,
        alsaSeq,
      }),
    [endpoints, registryDevices, registryEnabled, peers, alsaSeq],
  );

  const filtered = useMemo(() => {
    const list = mergedDevices.filter((row) => {
      const g = groupForMergedRow(row);
      if (g === "local" && !showLocal) return false;
      if (g === "remote" && !showRemote) return false;
      const pid = row.peerId;
      const connRouter =
        pid !== undefined ? (isPeerConnected.get(pid) ?? false) : false;
      const e = row.endpoint;
      const connAlsa =
        e?.kind === "alsa_seq" &&
        subsRows.some(
          (r) =>
            `alsa:${r.from_client}:${r.from_port}` === e.id ||
            `alsa:${r.to_client}:${r.to_port}` === e.id,
        );
      const conn = connRouter || connAlsa;
      if (connectedOnly && !conn) return false;
      if (!matchesMergedQuery(row, query)) return false;
      if (
        !showHidden &&
        (hiddenIds.has(row.id) || (e && autoHiddenIds.has(e.id)))
      ) {
        return false;
      }
      return true;
    });

    return [...list].sort((a, b) =>
      compareEndpointsForDevicesSort(
        a.sortEndpoint,
        b.sortEndpoint,
        sortKey,
        favoriteIds,
        isPeerConnected,
        byPeerId,
      ),
    );
  }, [
    mergedDevices,
    showLocal,
    showRemote,
    connectedOnly,
    query,
    sortKey,
    isPeerConnected,
    byPeerId,
    favoriteIds,
    showHidden,
    hiddenIds,
    autoHiddenIds,
    subsRows,
  ]);

  const [connectDialogForId, setConnectDialogForId] = useState<string | null>(
    null,
  );
  const [monitorFor, setMonitorFor] = useState<{
    id: string;
    label: string;
  } | null>(null);

  const doConnect = async (from: string, to: string) => {
    try {
      onStatus("");
      await rpc.call("endpoint.connect", { from, to, bidi: true });
      await onAfterAction();
      pulseEndpoints([from, to], from);
    } catch (e) {
      onStatus(String(e));
    }
  };

  const doDisconnect = async (from: string, to: string) => {
    try {
      onStatus("");
      await rpc.call("endpoint.disconnect", { from, to });
      await onAfterAction();
      pulseEndpoints([from, to], from);
    } catch (e) {
      onStatus(String(e));
    }
  };

  const Toggle = ({
    label,
    value,
    onChange,
  }: {
    label: string;
    value: boolean;
    onChange: (v: boolean) => void;
  }) => (
    <button
      type="button"
      onClick={() => onChange(!value)}
      class={`rounded-[var(--radius-sm)] px-2 py-1 font-mono text-[11px] font-black uppercase ${
        value ? "ui-toggle-on" : "ui-toggle-off"
      }`}
    >
      {label}
    </button>
  );

  const shown = showLocal || showRemote ? filtered : [];

  const removeManualDevice = async (identity: string) => {
    try {
      await rpc.call("devices.remove", { identity });
      await onAfterAction();
      onStatus("");
    } catch (e) {
      onStatus(String(e));
    }
  };

  return (
    <div class="space-y-4">
      <section class="ui-peer-card-shell">
        <div class="ui-devices-toolbar">
          <div class="ui-search-wrap">
            <input
              class="ui-input ui-search-input"
              value={query}
              onInput={(e) => setQuery((e.target as HTMLInputElement).value)}
              placeholder="Search name, host, kind…"
              aria-label="Search devices"
            />
            {query.trim() ? (
              <button
                type="button"
                class="ui-search-clear"
                aria-label="Clear search"
                title="Clear search"
                onClick={() => setQuery("")}
              >
                ×
              </button>
            ) : null}
          </div>
          <div class="ui-devices-toolbar-filters">
            <button
              type="button"
              onClick={() => setShowLocal(!showLocal)}
              class={`rounded-[var(--radius-sm)] px-2 py-1 font-mono text-[10px] font-black uppercase transition-[transform,opacity] duration-150 hover:opacity-95 active:scale-[0.98] sm:text-[11px] ${
                showLocal ? "ui-filter-toggle-local-on" : "ui-filter-toggle-local-off"
              }`}
            >
              Local
            </button>
            <button
              type="button"
              onClick={() => setShowRemote(!showRemote)}
              class={`rounded-[var(--radius-sm)] px-2 py-1 font-mono text-[10px] font-black uppercase transition-[transform,opacity] duration-150 hover:opacity-95 active:scale-[0.98] sm:text-[11px] ${
                showRemote ? "ui-filter-toggle-remote-on" : "ui-filter-toggle-remote-off"
              }`}
            >
              Remote
            </button>
            <Toggle
              label="Hidden"
              value={showHidden}
              onChange={setShowHiddenPersist}
            />
            <div class="flex items-center gap-1">
              <span class="hidden font-mono text-[10px] font-bold uppercase ui-text-muted sm:inline">
                Sort
              </span>
              <select
                class="ui-select mt-0 max-w-[7rem] py-1 font-mono text-[10px] font-bold sm:max-w-none sm:text-[11px]"
                value={sortKey}
                onChange={(e) =>
                  setSortKey((e.target as HTMLSelectElement).value as SortKey)
                }
                aria-label="Sort devices"
              >
                <option value="activity">Activity</option>
                <option value="connected">Connected</option>
                <option value="name">Name</option>
                <option value="kind">Kind</option>
              </select>
            </div>
            <span class="hidden font-mono text-[10px] ui-text-muted sm:inline">
              <span class="font-black ui-text">{shown.length}</span>
              <span class="ui-text-subtle">/{mergedDevices.length}</span>
            </span>
            {registryEnabled ? (
              <button
                type="button"
                class="ui-card-action ui-card-action-accent"
                onClick={() => setShowAddDevice(true)}
              >
                + Add
              </button>
            ) : null}
            <span class="font-mono text-[10px] ui-text-muted sm:hidden">
              <span class="font-black ui-text">{shown.length}</span>
              <span class="ui-text-subtle">/{mergedDevices.length}</span>
            </span>
            <Toggle
              label="Connected"
              value={connectedOnly}
              onChange={setConnectedOnly}
            />
          </div>
        </div>
      </section>

      <div class="grid gap-4">
        {shown.map((row) => {
          const e = row.endpoint;
          const actionEndpointId = row.connectEndpointId;
          const g = groupForMergedRow(row);
          const pid = row.peerId;
          const peer = pid !== undefined ? byPeerId.get(pid) : undefined;
          const connRouter =
            pid !== undefined ? (isPeerConnected.get(pid) ?? false) : false;
          const connAlsa =
            e?.kind === "alsa_seq" &&
            subsRows.some(
              (r) =>
                `alsa:${r.from_client}:${r.from_port}` === e.id ||
                `alsa:${r.to_client}:${r.to_port}` === e.id,
            );
          const conn = connRouter || connAlsa;
          const isSpotlight = spotlightIds.has(row.id);
          const spot = isSpotlight ? " ui-peer-card-spotlight" : "";
          const ring = highlightIds.has(row.id) ? " ui-tr-highlight" : "";
          const isManualHidden = hiddenIds.has(row.id);
          const isAutoHidden = e ? autoHiddenIds.has(e.id) : false;
          const hiddenVisual =
            showHidden && (isManualHidden || isAutoHidden);
          const offlineOnly = row.isOfflineOnly;
          const surf = isSpotlight
            ? ""
            : offlineOnly
              ? "bg-[color:var(--color-surface-zebra-b)] opacity-80"
              : conn
                ? "bg-[color:var(--color-surface)]"
                : "bg-[color:var(--color-surface-zebra-b)]";
          const cardShadow = isSpotlight ? "" : "ui-shadow-card ";
          const hiddenRing = hiddenVisual
            ? " outline outline-1 outline-dashed outline-[color:var(--color-border)] opacity-[0.93]"
            : "";
          const accentBorder =
            g === "local"
              ? conn && !offlineOnly
                ? "border-[color:var(--color-badge-local-border)]"
                : "border-[color:var(--color-badge-local-fg)]"
              : conn && !offlineOnly
                ? "border-[color:var(--color-badge-remote-border)]"
                : "border-[color:var(--color-badge-remote-fg)]";

          const outPeerIds = pid !== undefined && peer ? peer.send_to : [];
          const inPeerIds = pid !== undefined ? (recvFrom.get(pid) ?? []) : [];
          const outSet = new Set(outPeerIds);
          const inSet = new Set(inPeerIds);
          const connectedEndpoints =
            e && pid !== undefined
              ? Array.from(new Set([...outPeerIds, ...inPeerIds]))
                  .map((otherId) => {
                    const ce = resolveNeighborEndpoint(
                      otherId,
                      endpointByPeerId,
                      byPeerId,
                    );
                    const hasOut = outSet.has(otherId);
                    const hasIn = inSet.has(otherId);
                    const dir =
                      hasIn && hasOut
                        ? "IN-OUT"
                        : hasOut
                          ? "OUT"
                          : hasIn
                            ? "IN"
                            : "";
                    return { ce, dir };
                  })
                  .sort((a, b) => {
                    const c = a.ce.label.localeCompare(b.ce.label);
                    return c !== 0 ? c : a.ce.id.localeCompare(b.ce.id);
                  })
              : [];

          const alsaOut =
            e?.kind === "alsa_seq"
              ? subsRows.filter(
                  (r) => `alsa:${r.from_client}:${r.from_port}` === e.id,
                )
              : [];
          const alsaIn =
            e?.kind === "alsa_seq"
              ? subsRows.filter((r) => `alsa:${r.to_client}:${r.to_port}` === e.id)
              : [];

          return (
            <div
              key={row.id}
              ref={(el) => {
                if (el) cardRefs.current.set(row.id, el);
                else cardRefs.current.delete(row.id);
              }}
              class={`${isSpotlight ? "overflow-visible" : "overflow-hidden"} rounded-[var(--radius-md)] border-2 ${cardShadow}${surf} ${accentBorder}${spot}${ring}${hiddenRing}`}
            >
              <div class="ui-peer-card-head px-3 py-2">
                <div class="flex flex-wrap items-start justify-between gap-3">
                  <div class="min-w-0 flex-1">
                    <div class="flex min-w-0 items-center gap-2">
                      <button
                        type="button"
                        class="-ml-1 flex h-9 w-9 shrink-0 cursor-pointer items-center justify-center rounded-md font-mono text-2xl leading-none outline-none transition-all duration-200 ease-out ui-text-muted hover:scale-110 hover:bg-[color:var(--color-surface-2)] hover:text-[color:var(--color-ring-highlight)] hover:shadow-[0_0_0_1px_color-mix(in_srgb,var(--color-ring-highlight)_35%,transparent)] focus-visible:ring-2 focus-visible:ring-[color:var(--color-ring-highlight)] active:scale-95"
                        aria-label={
                          favoriteIds.has(row.id)
                            ? "Remove from favorites"
                            : "Add to favorites"
                        }
                        aria-pressed={favoriteIds.has(row.id)}
                        title={
                          favoriteIds.has(row.id)
                            ? `Remove from favorites. ${CONFIRM_SKIP_HINT}`
                            : "Add to favorites (stored in this browser)"
                        }
                        onClick={(ev) => {
                          ev.preventDefault();
                          ev.stopPropagation();
                          if (favoriteIds.has(row.id)) {
                            runWithConfirm(
                              ev,
                              `Remove "${row.label}" from favorites?`,
                              () => toggleFavorite(row.id),
                            );
                          } else {
                            toggleFavorite(row.id);
                          }
                        }}
                      >
                        {favoriteIds.has(row.id) ? (
                          <span
                            class="text-[color:var(--color-ring-highlight)] drop-shadow-[0_1px_3px_color-mix(in_srgb,var(--color-ring-highlight)_45%,transparent)]"
                            aria-hidden
                          >
                            ★
                          </span>
                        ) : (
                          <span aria-hidden>☆</span>
                        )}
                      </button>
                      {isManualHidden && showHidden ? (
                        <button
                          type="button"
                          class="ui-icon-btn -ml-0.5"
                          title={`Unhide (show in list). ${CONFIRM_SKIP_HINT}`}
                          aria-label="Unhide device"
                          onClick={(ev) => {
                            ev.preventDefault();
                            ev.stopPropagation();
                            runWithConfirm(
                              ev,
                              `Unhide "${row.label}" and show it in the device list again?`,
                              () => toggleManualHidden(row.id),
                            );
                          }}
                        >
                          <IconEyeOff />
                        </button>
                      ) : !isManualHidden && e ? (
                        <button
                          type="button"
                          class="ui-icon-btn -ml-0.5"
                          title="Hide from device list (stored in this browser)"
                          aria-label="Hide device"
                          onClick={(ev) => {
                            ev.preventDefault();
                            ev.stopPropagation();
                            toggleManualHidden(row.id);
                          }}
                        >
                          <IconEye />
                        </button>
                      ) : null}
                      <span class="min-w-0 truncate font-mono text-sm font-black ui-text">
                        {row.label}
                      </span>
                    </div>
                    <div class="mt-1 flex flex-wrap items-center gap-x-2 gap-y-1 font-mono text-[11px] ui-text-muted">
                      <AccentPill group={g} connected={conn && !offlineOnly}>
                        {g === "local" ? "LOCAL" : "REMOTE"}
                      </AccentPill>
                      <DeviceMetaTags
                        sourceTag={row.sourceTag}
                        statusTag={row.statusTag}
                        lastSeen={row.lastSeen}
                        showLastSeen={offlineOnly}
                      />
                      {e ? (
                        <span class="truncate font-mono text-[10px] font-bold uppercase tracking-wide ui-text-muted">
                          {e.kind}
                        </span>
                      ) : row.registry ? (
                        <span class="truncate font-mono text-[10px] font-bold uppercase tracking-wide ui-text-muted">
                          {row.registry.type}
                        </span>
                      ) : null}
                      {isAutoHidden ? (
                        <span
                          class="rounded border border-[color:var(--color-border)] px-1 py-0.5 font-mono text-[9px] font-black uppercase ui-text-subtle"
                          title="Matches an RTP listener exported by this rtpmidid; hidden by default"
                        >
                          bridge export
                        </span>
                      ) : null}
                      {isManualHidden ? (
                        <span class="rounded border border-[color:var(--color-border)] px-1 py-0.5 font-mono text-[9px] font-black uppercase ui-text-subtle">
                          hidden
                        </span>
                      ) : null}
                      {pid !== undefined ? (
                        <span class="ui-peer-cap">
                          peer #{pid}
                        </span>
                      ) : null}
                      {row.registry?.source === "manual" ? (
                        <>
                          <button
                            type="button"
                            class="ui-text-btn font-mono text-[10px] ui-text-muted"
                            title="Edit manual device"
                            onClick={() => setEditDevice(row.registry!)}
                          >
                            Edit
                          </button>
                          <button
                            type="button"
                            class="ui-text-btn ui-text-btn-danger font-mono text-[10px] ui-text-muted"
                            title={`Remove manual device. ${CONFIRM_SKIP_HINT}`}
                            onClick={(ev) => {
                              ev.preventDefault();
                              ev.stopPropagation();
                              runWithConfirm(
                                ev,
                                `Remove manual device "${row.label}" from the registry?`,
                                () => removeManualDevice(row.registry!.identity),
                              );
                            }}
                          >
                            Remove
                          </button>
                        </>
                      ) : null}
                      <span class="ui-text-subtle">·</span>
                      <span class="min-w-0 flex-1 truncate">{row.sub}</span>
                    </div>
                  </div>
                  <div class="flex shrink-0 flex-col items-end gap-1.5">
                    <div class="flex items-center gap-1.5">
                      <Led
                        label="IN"
                        active={pid !== undefined && inPulse.has(pid)}
                        group={g}
                        disabled={pid === undefined}
                      />
                      <Led
                        label="OUT"
                        active={pid !== undefined && outPulse.has(pid)}
                        group={g}
                        disabled={pid === undefined}
                      />
                    </div>
                    <span class="font-mono text-[10px] font-black uppercase tracking-wide ui-text-subtle">
                      {offlineOnly
                        ? "offline"
                        : conn
                          ? "connected"
                          : "disconnected"}
                    </span>
                  </div>
                </div>
              </div>

              <div class="p-3">
                <div class="space-y-2">
                  <div class="flex flex-wrap items-center gap-2 font-mono text-xs">
                      <span class="font-black uppercase ui-text-muted">
                        Stats
                      </span>
                      <span class="ui-text-subtle">·</span>
                      {peer ? (
                        <>
                          <span class="tabular-nums">
                            recv{" "}
                            <strong class="ui-text">
                              {peer.recv}
                            </strong>
                          </span>
                          <span class="tabular-nums">
                            sent{" "}
                            <strong class="ui-text">
                              {peer.sent}
                            </strong>
                          </span>
                          <span class="tabular-nums">
                            Σ{" "}
                            <strong class="ui-text">
                              {peer.recv + peer.sent}
                            </strong>
                          </span>
                          {peerCombinedLatencyMs(peer) !== null && conn ? (
                            <>
                              <span class="ui-text-subtle">·</span>
                              <span class="inline-flex items-center gap-2">
                                <span class="font-black uppercase ui-text-muted">
                                  Lat
                                </span>
                                <span class="relative min-w-[10rem]">
                                  <PeerLatencyHoverCell peer={peer} />
                                </span>
                              </span>
                            </>
                          ) : null}
                        </>
                      ) : (
                        <span class="ui-text-subtle">—</span>
                      )}
                    </div>

                    <div>
                      <div class="mb-1 font-mono text-[10px] font-black uppercase ui-text-muted">
                        connected to ({connectedEndpoints.length}{e?.kind === "alsa_seq" ? ` + ${alsaOut.length + alsaIn.length} alsa` : ""})
                      </div>
                      <div class="flex flex-wrap gap-1">
                        {connectedEndpoints.length ? (
                          connectedEndpoints.map(({ ce, dir }) => (
                            <span
                              key={ce.id}
                              class={`inline-flex items-center gap-1 rounded-[var(--radius-sm)] px-1 py-0.5 font-mono text-[11px] font-bold ${
                                groupForEndpoint(ce) === "local"
                                  ? "ui-badge-local"
                                  : "ui-badge-remote"
                              }`}
                              title={ce.id}
                            >
                              <button
                                type="button"
                                class="hover:underline ui-text"
                                onClick={() => pulseEndpoints([e!.id, ce.id], ce.id)}
                              >
                                {ce.label}
                              </button>
                              {dir ? (
                                <span class="ui-pill-id text-[9px]">{dir}</span>
                              ) : null}
                              <button
                                type="button"
                                class={`px-1 py-0.5 text-[10px] font-black uppercase ${
                                  groupForEndpoint(ce) === "local"
                                    ? "ui-wire-action-local"
                                    : "ui-wire-action-remote"
                                }`}
                                onClick={() => void doDisconnect(e!.id, ce.id)}
                                title="Disconnect"
                              >
                                x
                              </button>
                            </span>
                          ))
                        ) : null}
                        {e?.kind === "alsa_seq" ? (
                          <>
                            {alsaOut.map((r) => {
                              const fromId = `alsa:${r.from_client}:${r.from_port}`;
                              const toId = `alsa:${r.to_client}:${r.to_port}`;
                              const toLabel = r.to_label ?? toId;
                              return (
                                <span
                                  key={`${fromId}->${toId}`}
                                  class="ui-emerald-chip gap-1 px-1 py-0.5 text-[11px]"
                                  title={`${fromId} -> ${toId}`}
                                >
                                  <button
                                    type="button"
                                    class="hover:underline ui-text"
                                    onClick={() => pulseEndpoints([e!.id, toId], toId)}
                                  >
                                    {toLabel}
                                  </button>
                                  <span class="font-black">→</span>
                                  <button
                                    type="button"
                                    class="ui-wire-action-local px-1 py-0.5 text-[10px] font-black uppercase"
                                    onClick={() => void doDisconnect(fromId, toId)}
                                  >
                                    x
                                  </button>
                                </span>
                              );
                            })}
                            {alsaIn.map((r) => {
                              const fromId = `alsa:${r.from_client}:${r.from_port}`;
                              const toId = `alsa:${r.to_client}:${r.to_port}`;
                              const fromLabel = r.from_label ?? fromId;
                              return (
                                <span
                                  key={`${fromId}->${toId}`}
                                  class="ui-emerald-chip gap-1 px-1 py-0.5 text-[11px]"
                                  title={`${fromId} -> ${toId}`}
                                >
                                  <button
                                    type="button"
                                    class="hover:underline ui-text"
                                    onClick={() => pulseEndpoints([e!.id, fromId], fromId)}
                                  >
                                    {fromLabel}
                                  </button>
                                  <span class="font-black">→</span>
                                  <span class="ui-text-muted">
                                    this
                                  </span>
                                  <button
                                    type="button"
                                    class="ui-wire-action-local px-1 py-0.5 text-[10px] font-black uppercase"
                                    onClick={() => void doDisconnect(fromId, toId)}
                                  >
                                    x
                                  </button>
                                </span>
                              );
                            })}
                          </>
                        ) : null}
                        {connectedEndpoints.length === 0 &&
                        (e?.kind !== "alsa_seq" ||
                          (alsaOut.length + alsaIn.length) === 0) ? (
                          <span class="font-mono text-[11px] ui-text-subtle">
                            {offlineOnly ? "Waiting for device to come online" : "—"}
                          </span>
                        ) : null}
                      </div>
                    </div>
                  {actionEndpointId ? (
                    <div class="flex flex-wrap items-center justify-end gap-1 border-t border-[color:var(--color-border-muted)] pt-2">
                      <button
                        type="button"
                        class="ui-card-action"
                        onClick={() =>
                          setMonitorFor({ id: actionEndpointId, label: row.label })
                        }
                      >
                        Monitor
                      </button>
                      <button
                        type="button"
                        class="ui-card-action ui-card-action-accent"
                        onClick={() => setConnectDialogForId(actionEndpointId)}
                      >
                        Connect
                      </button>
                    </div>
                  ) : null}
                </div>
              </div>
            </div>
          );
        })}
      </div>

      {connectDialogForId !== null && (
        <EndpointPickerDialog
          title="Connect endpoint"
          description={
            <>
              Pick another endpoint to connect with{" "}
              <strong class="ui-text">
                {endpoints.find((x) => x.id === connectDialogForId)?.label ??
                  mergedDevices.find(
                    (r) => r.connectEndpointId === connectDialogForId,
                  )?.label ??
                  connectDialogForId}
              </strong>
              . ALSA vs router routing is chosen by the server.
            </>
          }
          endpoints={endpoints}
          excludeIds={[connectDialogForId]}
          favoriteIds={favoriteIds}
          sortKey={sortKey}
          isPeerConnected={isPeerConnected}
          byPeerId={byPeerId}
          confirmLabel="Connect ↔"
          confirmOnSecondClick
          onClose={() => setConnectDialogForId(null)}
          onConfirm={(otherId) => {
            const from = connectDialogForId;
            setConnectDialogForId(null);
            void doConnect(from, otherId);
          }}
        />
      )}

      {monitorFor !== null ? (
        <MidiMonitorModal
          endpointId={monitorFor.id}
          endpointLabel={monitorFor.label}
          rpc={rpc}
          onClose={() => setMonitorFor(null)}
          onStatus={onStatus}
        />
      ) : null}

      {showAddDevice && registryEnabled ? (
        <ManualDeviceDialog
          onClose={() => setShowAddDevice(false)}
          onSave={async (identity, name, previousIdentity) => {
            if (previousIdentity && previousIdentity !== identity) {
              await rpc.call("devices.remove", { identity: previousIdentity });
            }
            await rpc.call("devices.add_manual", {
              identity,
              ...(name ? { name } : {}),
            });
            await onAfterAction();
            onStatus("");
          }}
        />
      ) : null}

      {editDevice !== null ? (
        <ManualDeviceDialog
          title="Edit device"
          initialIdentity={editDevice.identity}
          initialName={editDevice.name}
          onClose={() => setEditDevice(null)}
          onSave={async (identity, name, previousIdentity) => {
            if (previousIdentity && previousIdentity !== identity) {
              await rpc.call("devices.remove", { identity: previousIdentity });
            }
            await rpc.call("devices.add_manual", {
              identity,
              ...(name ? { name } : {}),
            });
            await onAfterAction();
            onStatus("");
          }}
        />
      ) : null}

    </div>
  );
}

