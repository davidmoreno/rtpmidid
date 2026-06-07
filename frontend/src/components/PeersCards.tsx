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
import type { Endpoint, EndpointKind } from "../endpoints";
import {
  buildEndpoints,
  buildPickerEndpoints,
  collectBridgeExportedEndpointIds,
  endpointFromRouterPeer,
} from "../endpoints";
import { identityFromAlsaAddress, identityFromPeerRow } from "../deviceIdentity";
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
import { disconnectEndpoints } from "../disconnectEndpoint";

type SortKey = EndpointSortKey;

type DeviceFilterState = {
  types: Set<string>;
  groups: Set<string>;
  statuses: Set<string>;
  onlyFavourites: boolean;
};

const ALL_TYPES: EndpointKind[] = [
  "alsa_seq",
  "rawmidi",
  "rtpmidi",
  "monitor",
  "peer",
];

const TYPE_LABELS: Record<string, string> = {
  alsa_seq: "ALSA Seq",
  rawmidi: "Raw MIDI",
  rtpmidi: "RTP-MIDI",
  monitor: "Monitor",
  peer: "Router Peer",
};

function typeLabelForRow(row: MergedDeviceRow): string {
  if (row.endpoint) return row.endpoint.kind;
  if (row.registry) {
    const t = row.registry.type;
    if (t === "rtpmidi_client") return "rtpmidi";
    if (t === "alsa_seq") return "alsa_seq";
    if (t === "rawmidi") return "rawmidi";
  }
  return "registry";
}

function statusForRow(
  row: MergedDeviceRow,
  isPeerConnected: Map<number, boolean>,
): string {
  if (row.isOfflineOnly) return "offline";
  const pid = row.peerId;
  if (pid !== undefined && (isPeerConnected.get(pid) ?? false)) return "connected";
  return "disconnected";
}

function matchesFilter(
  row: MergedDeviceRow,
  filters: DeviceFilterState,
  isPeerConnected: Map<number, boolean>,
  favoriteIds: Set<string>,
  showHidden: boolean,
  hiddenIds: Set<string>,
  autoHiddenIds: Set<string>,
): boolean {
  if (filters.types.size > 0) {
    const rowType = typeLabelForRow(row);
    if (!filters.types.has(rowType)) return false;
  }

  if (filters.groups.size > 0) {
    const g = groupForMergedRow(row);
    if (!filters.groups.has(g)) return false;
  }

  if (filters.statuses.size > 0) {
    const st = statusForRow(row, isPeerConnected);
    if (!filters.statuses.has(st)) return false;
  }

  if (filters.onlyFavourites && !favoriteIds.has(row.id)) return false;

  const e = row.endpoint;
  if (
    !showHidden &&
    (hiddenIds.has(row.id) || (e && autoHiddenIds.has(e.identity)))
  ) {
    return false;
  }

  return true;
}

function DEFAULT_FILTERS(): DeviceFilterState {
  return {
    types: new Set<string>(),
    groups: new Set<string>(),
    statuses: new Set<string>(),
    onlyFavourites: false,
  };
}

function toggleInSet(set: Set<string>, key: string): Set<string> {
  const next = new Set(set);
  if (next.has(key)) next.delete(key);
  else next.add(key);
  return next;
}

function IconFilter({ class: className = "h-4 w-4" }: { class?: string }) {
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
      <polygon points="22 3 2 3 10 12.46 10 19 14 21 14 12.46 22 3" />
    </svg>
  );
}

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
  const identity = `alsa_seq:name=Peer #${otherId}`;
  return {
    identity,
    kind: "peer",
    label: `Peer #${otherId}`,
    sub: "missing from router status",
    peerId: otherId,
  };
}

function alsaSubTouchesEndpoint(
  r: {
    from_client: number;
    from_port: number;
    to_client: number;
    to_port: number;
    from_client_name?: string;
    from_port_name?: string;
    to_client_name?: string;
    to_port_name?: string;
  },
  endpoint: Endpoint,
  alsaSeq: MidiAlsaSeqEntry[],
): boolean {
  if (endpoint.kind !== "alsa_seq") return false;
  const fromId = identityFromAlsaAddress(
    r.from_client,
    r.from_port,
    r.from_client_name,
    r.from_port_name,
    alsaSeq,
  );
  const toId = identityFromAlsaAddress(
    r.to_client,
    r.to_port,
    r.to_client_name,
    r.to_port_name,
    alsaSeq,
  );
  return endpoint.identity === fromId || endpoint.identity === toId;
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
  connectionsDbEnabled?: boolean;
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
  connectionsDbEnabled = false,
  highlightEndpointId,
  rpc,
  onAfterAction,
  onStatus,
}: Props) {
  const [query, setQuery] = useState("");
  const [sortKey, setSortKey] = useState<SortKey>("activity");
  const [filters, setFilters] = useState<DeviceFilterState>(DEFAULT_FILTERS);
  const [filterPopoverOpen, setFilterPopoverOpen] = useState(false);
  const filterBtnRef = useRef<HTMLButtonElement>(null);
  const filterPopoverRef = useRef<HTMLDivElement>(null);
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

  const pickerEndpoints = useMemo(
    () =>
      buildPickerEndpoints({
        alsaSeq,
        rawmidi,
        mdnsRemotes,
        peers,
        registryDevices: registryEnabled ? registryDevices : [],
      }),
    [alsaSeq, rawmidi, mdnsRemotes, peers, registryDevices, registryEnabled],
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

  // Cooldown: clear IN/OUT LEDs 500 ms after the last pulse
  const pulseCooldownRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  useEffect(() => {
    if (inPulse.size === 0 && outPulse.size === 0) return;
    if (pulseCooldownRef.current) clearTimeout(pulseCooldownRef.current);
    pulseCooldownRef.current = setTimeout(() => {
      setInPulse(new Set());
      setOutPulse(new Set());
    }, 500);
    return () => {
      if (pulseCooldownRef.current) clearTimeout(pulseCooldownRef.current);
    };
  }, [inPulse, outPulse]);

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

  /* Close filter popover on outside click */
  useEffect(() => {
    if (!filterPopoverOpen) return;
    const onClick = (ev: MouseEvent) => {
      const target = ev.target as Node;
      if (
        filterBtnRef.current?.contains(target) ||
        filterPopoverRef.current?.contains(target)
      )
        return;
      setFilterPopoverOpen(false);
    };
    const onKey = (ev: KeyboardEvent) => {
      if (ev.key === "Escape") setFilterPopoverOpen(false);
    };
    document.addEventListener("mousedown", onClick);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("mousedown", onClick);
      document.removeEventListener("keydown", onKey);
    };
  }, [filterPopoverOpen]);

  const hasActiveFilters =
    filters.types.size > 0 ||
    filters.groups.size > 0 ||
    filters.statuses.size > 0 ||
    filters.onlyFavourites;

  const filtered = useMemo(() => {
    const list = mergedDevices.filter((row) => {
      if (!matchesFilter(row, filters, isPeerConnected, favoriteIds, showHidden, hiddenIds, autoHiddenIds)) return false;
      if (!matchesMergedQuery(row, query)) return false;
      return true;
    });

    return [...list].sort((a, b) => {
      // Favourites always first — use row id so registry-only rows also work
      const fa = favoriteIds.has(a.id) ? 1 : 0;
      const fb = favoriteIds.has(b.id) ? 1 : 0;
      if (fa !== fb) return fb - fa;
      return compareEndpointsForDevicesSort(
        a.sortEndpoint,
        b.sortEndpoint,
        sortKey,
        favoriteIds,
        isPeerConnected,
        byPeerId,
      );
    });
  }, [
    mergedDevices,
    filters,
    query,
    sortKey,
    isPeerConnected,
    byPeerId,
    favoriteIds,
    showHidden,
    hiddenIds,
    autoHiddenIds,
  ]);

  const [connectDialogForId, setConnectDialogForId] = useState<string | null>(
    null,
  );
  const [connectBidi, setConnectBidi] = useState(true);
  const [monitorFor, setMonitorFor] = useState<{
    identity: string;
    label: string;
  } | null>(null);

  const doConnect = async (from: string, to: string, bidi = true) => {
    try {
      onStatus("");
      await rpc.call("endpoint.connect", { from, to, bidi });
      await onAfterAction();
      pulseEndpoints([from, to], from);
    } catch (e) {
      onStatus(String(e));
    }
  };

  const requestDisconnect = (
    ev: MouseEvent,
    fromIdentity: string,
    toIdentity: string,
    fromLabel: string,
    toLabel: string,
    fromPeerId?: number,
    toPeerId?: number,
  ) => {
    runWithConfirm(
      ev,
      `Disconnect "${fromLabel}" from "${toLabel}"?`,
      () =>
        disconnectEndpoints(rpc, onStatus, async () => {
          await onAfterAction();
          pulseEndpoints([fromIdentity, toIdentity], fromIdentity);
        }, {
          fromIdentity,
          toIdentity,
          fromPeerId,
          toPeerId,
          fromLabel,
          toLabel,
          connectionsDbEnabled,
        }),
    );
  };

  const shown = filtered;

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
            <button
              type="button"
              class={`ui-search-clear ${query.trim() ? "" : "ui-search-clear--muted"}`}
              aria-label="Clear search"
              title="Clear search"
              onClick={() => setQuery("")}
              disabled={!query.trim()}
            >
              ×
            </button>
          </div>
          <div class="ui-devices-toolbar-filters">
            <div class="ui-filter-btn-wrap">
              <button
                ref={filterBtnRef}
                type="button"
                class={`ui-filter-btn ${hasActiveFilters ? "ui-filter-btn--active" : ""}`}
                aria-label="Filter devices"
                title="Filter devices"
                onClick={() => setFilterPopoverOpen((v) => !v)}
              >
                <IconFilter class="h-4 w-4" />
              </button>
              {filterPopoverOpen && (
                <div ref={filterPopoverRef} class="ui-filter-popover">
                  <div class="ui-filter-section">
                    <span class="ui-filter-section-title">Type</span>
                    <div class="ui-filter-chips">
                      {ALL_TYPES.map((t) => (
                        <button
                          key={t}
                          type="button"
                          class={`ui-filter-chip ${filters.types.has(t) ? "ui-filter-chip--on" : "ui-filter-chip--off"}`}
                          onClick={() =>
                            setFilters((f) => ({
                              ...f,
                              types: toggleInSet(f.types, t),
                            }))
                          }
                        >
                          {TYPE_LABELS[t] ?? t}
                        </button>
                      ))}
                    </div>
                  </div>
                  <div class="ui-filter-section">
                    <span class="ui-filter-section-title">Group</span>
                    <div class="ui-filter-chips">
                      {(["local", "remote"] as const).map((g) => (
                        <button
                          key={g}
                          type="button"
                          class={`ui-filter-chip ${filters.groups.has(g) ? `ui-filter-chip--${g}-on` : "ui-filter-chip--off"}`}
                          onClick={() =>
                            setFilters((f) => ({
                              ...f,
                              groups: toggleInSet(f.groups, g),
                            }))
                          }
                        >
                          {g === "local" ? "Local" : "Remote"}
                        </button>
                      ))}
                    </div>
                  </div>
                  <div class="ui-filter-section">
                    <span class="ui-filter-section-title">Status</span>
                    <div class="ui-filter-chips">
                      {(["connected", "disconnected", "offline"] as const).map((s) => (
                        <button
                          key={s}
                          type="button"
                          class={`ui-filter-chip ${filters.statuses.has(s) ? `ui-filter-chip--${s}-on` : "ui-filter-chip--off"}`}
                          onClick={() =>
                            setFilters((f) => ({
                              ...f,
                              statuses: toggleInSet(f.statuses, s),
                            }))
                          }
                        >
                          {s.charAt(0).toUpperCase() + s.slice(1)}
                        </button>
                      ))}
                    </div>
                  </div>
                  <div class="ui-filter-section">
                    <span class="ui-filter-section-title">Other</span>
                    <div class="ui-filter-chips">
                      <button
                        type="button"
                        class={`ui-filter-chip ${filters.onlyFavourites ? "ui-filter-chip--fav-on" : "ui-filter-chip--off"}`}
                        onClick={() =>
                          setFilters((f) => ({
                            ...f,
                            onlyFavourites: !f.onlyFavourites,
                          }))
                        }
                      >
                        ★ Favourites
                      </button>
                      <button
                        type="button"
                        class={`ui-filter-chip ${showHidden ? "ui-filter-chip--on" : "ui-filter-chip--off"}`}
                        onClick={() => setShowHiddenPersist(!showHidden)}
                      >
                        <span class="inline-flex items-center gap-1">
                          <IconEyeOff class="h-3 w-3" /> Hidden
                        </span>
                      </button>
                    </div>
                  </div>
                  <div class="ui-filter-section">
                    <button
                      type="button"
                      class="ui-filter-reset"
                      onClick={() => {
                        setFilters(DEFAULT_FILTERS());
                        setShowHiddenPersist(false);
                      }}
                    >
                      Reset all filters
                    </button>
                  </div>
                </div>
              )}
            </div>
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
          </div>
        </div>
      </section>

      <div class="grid gap-4 overflow-x-hidden [&>*]:min-w-0 [&>*]:max-w-full">
        {shown.map((row) => {
          const e = row.endpoint;
          const actionIdentity = row.connectIdentity;
          const g = groupForMergedRow(row);
          const pid = row.peerId;
          const peer = pid !== undefined ? byPeerId.get(pid) : undefined;
          const connRouter =
            pid !== undefined ? (isPeerConnected.get(pid) ?? false) : false;
          const connAlsa =
            e?.kind === "alsa_seq" &&
            subsRows.some((r) => alsaSubTouchesEndpoint(r, e, alsaSeq));
          const conn = connRouter || connAlsa;
          const isSpotlight = spotlightIds.has(row.id);
          const spot = isSpotlight ? " ui-peer-card-spotlight" : "";
          const ring = highlightIds.has(row.id) ? " ui-tr-highlight" : "";
          const isManualHidden = hiddenIds.has(row.id);
          const isAutoHidden = e ? autoHiddenIds.has(e.identity) : false;
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
                    return c !== 0 ? c : a.ce.identity.localeCompare(b.ce.identity);
                  })
              : [];

          const alsaOut =
            e?.kind === "alsa_seq"
              ? subsRows.filter((r) => alsaSubTouchesEndpoint(r, e, alsaSeq) &&
                  identityFromAlsaAddress(
                    r.from_client,
                    r.from_port,
                    r.from_client_name,
                    r.from_port_name,
                    alsaSeq,
                  ) === e.identity)
              : [];
          const alsaIn =
            e?.kind === "alsa_seq"
              ? subsRows.filter((r) => alsaSubTouchesEndpoint(r, e, alsaSeq) &&
                  identityFromAlsaAddress(
                    r.to_client,
                    r.to_port,
                    r.to_client_name,
                    r.to_port_name,
                    alsaSeq,
                  ) === e.identity)
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
                              key={ce.identity}
                              class={`inline-flex items-center gap-1 rounded-[var(--radius-sm)] px-1 py-0.5 font-mono text-[11px] font-bold ${
                                groupForEndpoint(ce) === "local"
                                  ? "ui-badge-local"
                                  : "ui-badge-remote"
                              }`}
                              title={ce.identity}
                            >
                              <button
                                type="button"
                                class="hover:underline ui-text"
                                onClick={() =>
                                  pulseEndpoints([e!.identity, ce.identity], ce.identity)
                                }
                              >
                                {ce.label}
                              </button>
                              {dir ? (
                                <span class="ui-pill-id text-[9px]">{dir}</span>
                              ) : null}
                              <button
                                type="button"
                                class={`ui-btn-plain px-1 py-0.5 text-[10px] font-black uppercase ${
                                  groupForEndpoint(ce) === "local"
                                    ? "ui-wire-action-local"
                                    : "ui-wire-action-remote"
                                }`}
                                title={`Disconnect. ${CONFIRM_SKIP_HINT}`}
                                onClick={(ev) =>
                                  requestDisconnect(
                                    ev,
                                    actionIdentity ?? e!.identity,
                                    ce.identity,
                                    row.label,
                                    ce.label,
                                    pid,
                                    ce.peerId,
                                  )
                                }
                              >
                                x
                              </button>
                            </span>
                          ))
                        ) : null}
                        {e?.kind === "alsa_seq" ? (
                          <>
                            {alsaOut.map((r) => {
                              const fromId = identityFromAlsaAddress(
                                r.from_client,
                                r.from_port,
                                r.from_client_name,
                                r.from_port_name,
                                alsaSeq,
                              );
                              const toId = identityFromAlsaAddress(
                                r.to_client,
                                r.to_port,
                                r.to_client_name,
                                r.to_port_name,
                                alsaSeq,
                              );
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
                                    onClick={() =>
                                      pulseEndpoints([e!.identity, toId], toId)
                                    }
                                  >
                                    {toLabel}
                                  </button>
                                  <span class="font-black">→</span>
                                  <button
                                    type="button"
                                    class="ui-btn-plain ui-wire-action-local px-1 py-0.5 text-[10px] font-black uppercase"
                                    title={`Disconnect. ${CONFIRM_SKIP_HINT}`}
                                    onClick={(ev) =>
                                      requestDisconnect(
                                        ev,
                                        fromId,
                                        toId,
                                        row.label,
                                        toLabel,
                                        pid,
                                        undefined,
                                      )
                                    }
                                  >
                                    x
                                  </button>
                                </span>
                              );
                            })}
                            {alsaIn.map((r) => {
                              const fromId = identityFromAlsaAddress(
                                r.from_client,
                                r.from_port,
                                r.from_client_name,
                                r.from_port_name,
                                alsaSeq,
                              );
                              const toId = identityFromAlsaAddress(
                                r.to_client,
                                r.to_port,
                                r.to_client_name,
                                r.to_port_name,
                                alsaSeq,
                              );
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
                                    onClick={() =>
                                      pulseEndpoints([e!.identity, fromId], fromId)
                                    }
                                  >
                                    {fromLabel}
                                  </button>
                                  <span class="font-black">→</span>
                                  <span class="ui-text-muted">
                                    this
                                  </span>
                                  <button
                                    type="button"
                                    class="ui-btn-plain ui-wire-action-local px-1 py-0.5 text-[10px] font-black uppercase"
                                    title={`Disconnect. ${CONFIRM_SKIP_HINT}`}
                                    onClick={(ev) =>
                                      requestDisconnect(
                                        ev,
                                        fromId,
                                        toId,
                                        fromLabel,
                                        row.label,
                                        undefined,
                                        pid,
                                      )
                                    }
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
                  {actionIdentity ? (
                    <div class="flex flex-wrap items-center justify-end gap-1 border-t border-[color:var(--color-border-muted)] pt-2">
                      <button
                        type="button"
                        class="ui-card-action"
                        onClick={() =>
                          setMonitorFor({
                            identity: actionIdentity,
                            label: row.label,
                          })
                        }
                      >
                        Monitor
                      </button>
                      <button
                        type="button"
                        class="ui-card-action ui-card-action-accent"
                        onClick={() => {
                          setConnectBidi(true);
                          setConnectDialogForId(actionIdentity);
                        }}
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
                {endpoints.find((x) => x.identity === connectDialogForId)?.label ??
                  mergedDevices.find(
                    (r) => r.connectIdentity === connectDialogForId,
                  )?.label ??
                  connectDialogForId}
              </strong>
              . ALSA↔ALSA uses kernel aconnect; other pairs use the router.
              Offline known devices (registry) create an RTP-MIDI client when
              you connect. Click a row twice, or select once and press Connect.
              {endpoints.find((x) => x.identity === connectDialogForId)?.kind ===
              "alsa_seq" ? (
                <label class="mt-2 flex cursor-pointer items-center gap-2">
                  <input
                    type="checkbox"
                    checked={connectBidi}
                    onChange={(e) =>
                      setConnectBidi((e.target as HTMLInputElement).checked)
                    }
                  />
                  Bidirectional (↔)
                </label>
              ) : null}
            </>
          }
          endpoints={pickerEndpoints}
          excludeIds={[connectDialogForId]}
          favoriteIds={favoriteIds}
          sortKey={sortKey}
          isPeerConnected={isPeerConnected}
          byPeerId={byPeerId}
          confirmLabel={connectBidi ? "Connect ↔" : "Connect →"}
          confirmOnSecondClick
          onClose={() => setConnectDialogForId(null)}
          onConfirm={(otherId) => {
            const from = connectDialogForId;
            const bidi = connectBidi;
            setConnectDialogForId(null);
            void doConnect(from, otherId, bidi);
          }}
        />
      )}

      {monitorFor !== null ? (
        <MidiMonitorModal
          identity={monitorFor.identity}
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

