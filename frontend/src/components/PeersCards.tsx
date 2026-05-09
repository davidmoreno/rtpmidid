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
import { Button } from "./Button";
import { MidiMonitorModal } from "./MidiMonitorModal";
import { PeerLatencyHoverCell, peerCombinedLatencyMs } from "./LatencyBar";

type EndpointGroup = "local" | "remote";
type SortKey = "activity" | "name" | "kind" | "connected";

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

function groupForEndpoint(e: Endpoint): EndpointGroup {
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
function compareEndpointsForDevicesSort(
  a: Endpoint,
  b: Endpoint,
  sortKey: SortKey,
  favoriteIds: Set<string>,
  isPeerConnected: Map<number, boolean>,
  byPeerId: Map<number, RouterPeer>,
): number {
  const fa = favoriteIds.has(a.id) ? 1 : 0;
  const fb = favoriteIds.has(b.id) ? 1 : 0;
  if (fa !== fb) return fb - fa;

  const activity = (e: Endpoint) => endpointActivity(e, byPeerId);
  const connFlag = (e: Endpoint): number =>
    e.peerId !== undefined && (isPeerConnected.get(e.peerId) ?? false) ? 1 : 0;

  if (sortKey === "name") {
    const c = a.label.localeCompare(b.label);
    return c !== 0 ? c : a.id.localeCompare(b.id);
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

function SelectBadge({ e }: { e: Endpoint }) {
  const g = groupForEndpoint(e);
  const cls =
    g === "local"
      ? "ui-badge-local inline-flex items-center gap-1 rounded px-1 py-0.5 font-mono text-[11px] font-bold"
      : "ui-badge-remote inline-flex items-center gap-1 rounded px-1 py-0.5 font-mono text-[11px] font-bold";
  return (
    <span class={cls}>
      <span class="max-w-[12rem] truncate">{e.label}</span>
      <span class="text-[9px] font-black uppercase opacity-80">{e.kind}</span>
    </span>
  );
}

function ConnectPeerDialog({
  selfId,
  endpoints,
  favoriteIds,
  sortKey,
  isPeerConnected,
  byPeerId,
  onClose,
  onConnect,
}: {
  selfId: string;
  endpoints: Endpoint[];
  favoriteIds: Set<string>;
  sortKey: SortKey;
  isPeerConnected: Map<number, boolean>;
  byPeerId: Map<number, RouterPeer>;
  onClose: () => void;
  onConnect: (otherId: string) => void;
}) {
  const [q, setQ] = useState("");
  const [selectedId, setSelectedId] = useState<string | null>(null);

  const opts = useMemo(() => endpoints.filter((x) => x.id !== selfId), [endpoints, selfId]);
  const filtered = useMemo(() => {
    const s = q.trim().toLowerCase();
    const base = !s
      ? opts
      : opts.filter((e) =>
          [e.label, e.sub, e.kind, e.id].join(" ").toLowerCase().includes(s),
        );
    return [...base].sort((a, b) =>
      compareEndpointsForDevicesSort(
        a,
        b,
        sortKey,
        favoriteIds,
        isPeerConnected,
        byPeerId,
      ),
    );
  }, [opts, q, favoriteIds, sortKey, isPeerConnected, byPeerId]);

  useEffect(() => {
    setQ("");
    setSelectedId(null);
  }, [selfId]);

  useEffect(() => {
    const onKey = (ev: KeyboardEvent) => {
      if (ev.key === "Escape") {
        ev.preventDefault();
        onClose();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);

  const selectedEp =
    selectedId !== null ? opts.find((x) => x.id === selectedId) ?? null : null;

  return (
    <div
      role="presentation"
      class="ui-modal-backdrop"
      onClick={() => onClose()}
    >
      <div
        role="dialog"
        aria-modal="true"
        aria-labelledby="connect-peer-title"
        class="ui-modal max-h-[min(90vh,36rem)]"
        onClick={(ev) => ev.stopPropagation()}
      >
        <h2
          id="connect-peer-title"
          class="mb-3 font-mono text-sm font-bold uppercase ui-text"
        >
          Connect endpoint
        </h2>
        <p class="mb-3 font-mono text-[11px] leading-relaxed ui-text-muted">
          Pick another endpoint to connect with{" "}
          <strong class="ui-text">
            {endpoints.find((x) => x.id === selfId)?.label ?? selfId}
          </strong>
          . ALSA vs router routing is chosen by the server.
        </p>

        <label class="mb-2 block font-mono text-[10px] font-bold uppercase ui-text-muted">
          Search
          <input
            value={q}
            onInput={(e) => setQ((e.target as HTMLInputElement).value)}
            placeholder="Filter by name, kind, id…"
            class="ui-input mt-1 font-mono text-[11px]"
            autoFocus
          />
        </label>

        <div class="max-h-[min(40vh,14rem)] overflow-y-auto rounded-[var(--radius-md)] border border-[color:var(--color-border)] p-2">
          {filtered.length ? (
            <div class="space-y-1">
              {filtered.map((ep) => {
                const g = groupForEndpoint(ep);
                const base =
                  g === "local" ? "ui-endpoint-opt-local" : "ui-endpoint-opt-remote";
                const sel = ep.id === selectedId;
                return (
                  <button
                    type="button"
                    key={ep.id}
                    class={`w-full rounded-[var(--radius-sm)] border-2 p-2 text-left font-mono ${base} ${
                      sel ? "ring-2 ring-[color:var(--color-ring-highlight)] ring-inset" : ""
                    }`}
                    onClick={() => {
                      if (selectedId === ep.id) {
                        onConnect(ep.id);
                      } else {
                        setSelectedId(ep.id);
                      }
                    }}
                    title={
                      selectedId === ep.id
                        ? `${ep.id} — click again to connect`
                        : ep.id
                    }
                  >
                    <div class="flex flex-wrap items-center justify-between gap-2">
                      <div class="min-w-0">
                        <div class="truncate text-xs font-black">{ep.label}</div>
                        <div class="truncate text-[10px] font-bold opacity-80">{ep.sub}</div>
                      </div>
                      <div class="shrink-0 text-right">
                        <div class="text-[10px] font-black uppercase">{ep.kind}</div>
                        <div class="text-[10px] opacity-80">{ep.id}</div>
                      </div>
                    </div>
                  </button>
                );
              })}
            </div>
          ) : (
            <div class="font-mono text-[11px] ui-text-subtle">No matching endpoints.</div>
          )}
        </div>

        {selectedEp ? (
          <p class="mt-2 font-mono text-[10px] ui-text-muted">
            Selected: <SelectBadge e={selectedEp} />
          </p>
        ) : null}

        <div class="mt-4 flex flex-wrap gap-2">
          <Button type="button" onClick={onClose}>
            Cancel
          </Button>
          <Button
            disabled={selectedId === null}
            onClick={() => {
              if (selectedId !== null) onConnect(selectedId);
            }}
          >
            Connect ↔
          </Button>
        </div>
      </div>
    </div>
  );
}

function matchesQuery(e: Endpoint, q: string): boolean {
  const query = q.trim().toLowerCase();
  if (!query) return true;
  const hay = [e.id, e.kind, e.label, e.sub].join(" ").toLowerCase();
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
  rpc,
  onAfterAction,
  onStatus,
}: Props) {
  const [showLocal, setShowLocal] = useState(true);
  const [showRemote, setShowRemote] = useState(true);
  const [connectedOnly, setConnectedOnly] = useState(false);
  const [query, setQuery] = useState("");
  const [sortKey, setSortKey] = useState<SortKey>("activity");

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

  const filtered = useMemo(() => {
    const list = endpoints.filter((e) => {
      const g = groupForEndpoint(e);
      if (g === "local" && !showLocal) return false;
      if (g === "remote" && !showRemote) return false;
      const conn = e.peerId !== undefined ? (isPeerConnected.get(e.peerId) ?? false) : false;
      if (connectedOnly && !conn) return false;
      if (!matchesQuery(e, query)) return false;
      if (
        !showHidden &&
        (hiddenIds.has(e.id) || autoHiddenIds.has(e.id))
      ) {
        return false;
      }
      return true;
    });

    return [...list].sort((a, b) =>
      compareEndpointsForDevicesSort(
        a,
        b,
        sortKey,
        favoriteIds,
        isPeerConnected,
        byPeerId,
      ),
    );
  }, [
    endpoints,
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
  ]);

  const hiddenEligibleCount = useMemo(
    () =>
      endpoints.filter(
        (e) => hiddenIds.has(e.id) || autoHiddenIds.has(e.id),
      ).length,
    [endpoints, hiddenIds, autoHiddenIds],
  );

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

  const shown = (showLocal || showRemote) ? filtered : [];

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

  return (
    <div class="space-y-4">
      <section class="ui-peer-card-shell">
        <div class="flex flex-wrap items-end justify-between gap-3">
          <div class="flex flex-wrap items-center gap-2">
            <button
              type="button"
              onClick={() => setShowLocal(!showLocal)}
              class={`rounded-[var(--radius-sm)] px-2 py-1 font-mono text-[11px] font-black uppercase transition-[transform,opacity] duration-150 hover:opacity-95 active:scale-[0.98] ${
                showLocal ? "ui-filter-toggle-local-on" : "ui-filter-toggle-local-off"
              }`}
            >
              Local
            </button>
            <button
              type="button"
              onClick={() => setShowRemote(!showRemote)}
              class={`rounded-[var(--radius-sm)] px-2 py-1 font-mono text-[11px] font-black uppercase transition-[transform,opacity] duration-150 hover:opacity-95 active:scale-[0.98] ${
                showRemote ? "ui-filter-toggle-remote-on" : "ui-filter-toggle-remote-off"
              }`}
            >
              Remote
            </button>
            <Toggle
              label="Connected"
              value={connectedOnly}
              onChange={setConnectedOnly}
            />
            <Toggle
              label="Show hidden"
              value={showHidden}
              onChange={setShowHiddenPersist}
            />
            <span class="font-mono text-[10px] ui-text-muted">
              Showing{" "}
              <span class="font-black ui-text">{shown.length}</span>
              <span class="ui-text-subtle"> / {endpoints.length}</span> endpoints
              {hiddenEligibleCount > 0 ? (
                <span class="ui-text-subtle">
                  {!showHidden
                    ? ` · ${hiddenEligibleCount} hidden`
                    : ` · ${hiddenEligibleCount} incl. hidden`}
                </span>
              ) : null}
            </span>
            <div class="ml-2 flex items-center gap-2">
              <span class="font-mono text-[11px] font-bold uppercase ui-text-muted">
                Sort
              </span>
              <select
                class="ui-select mt-0 py-1 font-mono text-[11px] font-bold"
                value={sortKey}
                onChange={(e) =>
                  setSortKey((e.target as HTMLSelectElement).value as SortKey)
                }
              >
                <option value="activity">Activity</option>
                <option value="connected">Connected</option>
                <option value="name">Name</option>
                <option value="kind">Kind</option>
              </select>
            </div>
          </div>
          <label class="min-w-[14rem] grow font-mono text-[11px] font-bold uppercase ui-text-muted">
            Search
            <input
              class="ui-input mt-1 text-xs"
              value={query}
              onInput={(e) => setQuery((e.target as HTMLInputElement).value)}
              placeholder="name, device, host, kind…"
            />
          </label>
        </div>
      </section>

      <div class="grid gap-4">
        {shown.map((e) => {
          const g = groupForEndpoint(e);
          const pid = e.peerId;
          const peer = pid !== undefined ? byPeerId.get(pid) : undefined;
          const connRouter = pid !== undefined ? (isPeerConnected.get(pid) ?? false) : false;
          const connAlsa =
            e.kind === "alsa_seq" &&
            subsRows.some(
              (r) =>
                `alsa:${r.from_client}:${r.from_port}` === e.id ||
                `alsa:${r.to_client}:${r.to_port}` === e.id,
            );
          const conn = connRouter || connAlsa;
          const isSpotlight = spotlightIds.has(e.id);
          const spot = isSpotlight ? " ui-peer-card-spotlight" : "";
          const ring = highlightIds.has(e.id) ? " ui-tr-highlight" : "";
          const isManualHidden = hiddenIds.has(e.id);
          const isAutoHidden = autoHiddenIds.has(e.id);
          const hiddenVisual =
            showHidden && (isManualHidden || isAutoHidden);
          /* Avoid Tailwind bg-* / ui-shadow-card overriding .ui-peer-card-spotlight */
          const surf = isSpotlight
            ? ""
            : conn
              ? "bg-[color:var(--color-surface)]"
              : "bg-[color:var(--color-surface-zebra-b)]";
          const cardShadow = isSpotlight ? "" : "ui-shadow-card ";
          const hiddenRing = hiddenVisual
            ? " outline outline-1 outline-dashed outline-[color:var(--color-border)] opacity-[0.93]"
            : "";
          const accentBorder =
            g === "local"
              ? conn
                ? "border-[color:var(--color-badge-local-border)]"
                : "border-[color:var(--color-badge-local-fg)]"
              : conn
                ? "border-[color:var(--color-badge-remote-border)]"
                : "border-[color:var(--color-badge-remote-fg)]";

          const outPeerIds = pid !== undefined && peer ? peer.send_to : [];
          const inPeerIds = pid !== undefined ? (recvFrom.get(pid) ?? []) : [];
          const outSet = new Set(outPeerIds);
          const inSet = new Set(inPeerIds);
          const connectedEndpoints = Array.from(
            new Set([...outPeerIds, ...inPeerIds]),
          )
            .map((otherId) => {
              const ce = resolveNeighborEndpoint(
                otherId,
                endpointByPeerId,
                byPeerId,
              );
              const hasOut = outSet.has(otherId);
              const hasIn = inSet.has(otherId);
              const dir =
                hasIn && hasOut ? "IN-OUT" : hasOut ? "OUT" : hasIn ? "IN" : "";
              return { ce, dir };
            })
            .sort((a, b) => {
              const c = a.ce.label.localeCompare(b.ce.label);
              return c !== 0 ? c : a.ce.id.localeCompare(b.ce.id);
            });

          const alsaOut =
            e.kind === "alsa_seq"
              ? subsRows.filter(
                  (r) => `alsa:${r.from_client}:${r.from_port}` === e.id,
                )
              : [];
          const alsaIn =
            e.kind === "alsa_seq"
              ? subsRows.filter((r) => `alsa:${r.to_client}:${r.to_port}` === e.id)
              : [];

          return (
            <div
              key={e.id}
              ref={(el) => {
                if (el) cardRefs.current.set(e.id, el);
                else cardRefs.current.delete(e.id);
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
                          favoriteIds.has(e.id)
                            ? "Remove from favorites"
                            : "Add to favorites"
                        }
                        aria-pressed={favoriteIds.has(e.id)}
                        title="Favorite (stored in this browser)"
                        onClick={(ev) => {
                          ev.preventDefault();
                          ev.stopPropagation();
                          toggleFavorite(e.id);
                        }}
                      >
                        {favoriteIds.has(e.id) ? (
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
                          class="flex h-8 shrink-0 items-center rounded-md border border-[color:var(--color-border)] px-2 font-mono text-[10px] font-black uppercase ui-text-muted transition-colors hover:bg-[color:var(--color-surface-2)] hover:ui-text"
                          title="Remove from hidden list (stored in this browser)"
                          onClick={(ev) => {
                            ev.preventDefault();
                            ev.stopPropagation();
                            toggleManualHidden(e.id);
                          }}
                        >
                          Unhide
                        </button>
                      ) : !isManualHidden ? (
                        <button
                          type="button"
                          class="flex h-8 shrink-0 items-center rounded-md border border-[color:var(--color-border)] px-2 font-mono text-[10px] font-black uppercase ui-text-muted transition-colors hover:bg-[color:var(--color-surface-2)] hover:ui-text"
                          title="Hide from device list (stored in this browser)"
                          onClick={(ev) => {
                            ev.preventDefault();
                            ev.stopPropagation();
                            toggleManualHidden(e.id);
                          }}
                        >
                          Hide
                        </button>
                      ) : null}
                      <span class="min-w-0 truncate font-mono text-sm font-black ui-text">
                        {e.label}
                      </span>
                    </div>
                    <div class="mt-1 flex flex-wrap items-center gap-x-2 gap-y-1 font-mono text-[11px] ui-text-muted">
                      <AccentPill group={g} connected={conn}>
                        {g === "local" ? "LOCAL" : "REMOTE"}
                      </AccentPill>
                      <span class="truncate font-mono text-[10px] font-bold uppercase tracking-wide ui-text-muted">
                        {e.kind}
                      </span>
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
                      <span class="ui-text-subtle">·</span>
                      <span class="min-w-0 flex-1 truncate">{e.sub}</span>
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
                      {conn ? "connected" : "disconnected"}
                    </span>
                  </div>
                </div>
              </div>

              <div class="p-3">
                <div class="grid gap-3 md:grid-cols-[1fr_auto] md:items-start">
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
                          {peerCombinedLatencyMs(peer) !== null ? (
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
                        connected to ({connectedEndpoints.length}{e.kind === "alsa_seq" ? ` + ${alsaOut.length + alsaIn.length} alsa` : ""})
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
                                onClick={() => pulseEndpoints([e.id, ce.id], ce.id)}
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
                                onClick={() => void doDisconnect(e.id, ce.id)}
                                title="Disconnect"
                              >
                                x
                              </button>
                            </span>
                          ))
                        ) : null}
                        {e.kind === "alsa_seq" ? (
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
                                    onClick={() => pulseEndpoints([e.id, toId], toId)}
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
                                    onClick={() => pulseEndpoints([e.id, fromId], fromId)}
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
                        (e.kind !== "alsa_seq" || (alsaOut.length + alsaIn.length) === 0) ? (
                          <span class="font-mono text-[11px] ui-text-subtle">—</span>
                        ) : null}
                      </div>
                    </div>
                  </div>
                  <div class="flex shrink-0 flex-col items-end justify-end gap-2 sm:flex-row md:pt-0">
                    <Button
                      type="button"
                      onClick={() =>
                        setMonitorFor({ id: e.id, label: e.label })
                      }
                    >
                      Monitor
                    </Button>
                    <Button
                      disabled={endpoints.length <= 1}
                      onClick={() => setConnectDialogForId(e.id)}
                    >
                      Connect…
                    </Button>
                  </div>
                </div>
              </div>
            </div>
          );
        })}
      </div>

      {connectDialogForId !== null && (
        <ConnectPeerDialog
          selfId={connectDialogForId}
          endpoints={endpoints}
          favoriteIds={favoriteIds}
          sortKey={sortKey}
          isPeerConnected={isPeerConnected}
          byPeerId={byPeerId}
          onClose={() => setConnectDialogForId(null)}
          onConnect={(otherId) => {
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

    </div>
  );
}

