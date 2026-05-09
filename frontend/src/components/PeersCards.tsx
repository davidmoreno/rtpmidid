import { useEffect, useMemo, useRef, useState } from "preact/hooks";
import type { MdnsRemote, RouterPeer } from "../model";
import { buildRecvFromMap } from "../model";
import type { MidiAlsaSeqEntry, MidiRawmidiEntry } from "../midiEnumerate";
import type { RpcClient } from "../rpc";
import type { Endpoint } from "../endpoints";
import { buildEndpoints } from "../endpoints";
import { Button } from "./Button";
import { PeerLatencyHoverCell, peerCombinedLatencyMs } from "./LatencyBar";

type EndpointGroup = "local" | "remote";
type SortKey = "activity" | "name" | "kind" | "connected";

function groupForEndpoint(e: Endpoint): EndpointGroup {
  return e.kind === "remote" ? "remote" : "local";
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

function FancyEndpointSelect({
  selfId,
  endpoints,
  value,
  onChange,
}: {
  selfId: string;
  endpoints: Endpoint[];
  value: string | null;
  onChange: (id: string) => void;
}) {
  const [q, setQ] = useState("");
  const opts = useMemo(() => endpoints.filter((x) => x.id !== selfId), [endpoints, selfId]);
  const selected = useMemo(
    () => (value ? opts.find((x) => x.id === value) ?? null : null),
    [opts, value],
  );
  const filtered = useMemo(() => {
    const s = q.trim().toLowerCase();
    if (!s) return opts;
    return opts.filter((e) =>
      [e.label, e.sub, e.kind, e.id].join(" ").toLowerCase().includes(s),
    );
  }, [opts, q]);

  return (
    <details class="group relative">
      <summary class="ui-details-summary list-none cursor-pointer">
        <span class="flex items-center justify-between gap-2">
          <span class="min-w-0">
            {selected ? (
              <SelectBadge e={selected} />
            ) : (
              <span class="ui-text-muted">Choose endpoint…</span>
            )}
          </span>
          <span class="font-black ui-text-muted">▾</span>
        </span>
      </summary>

      <div class="ui-details-panel absolute z-50 mt-1 w-full">
        <div class="ui-details-search-wrap">
          <input
            value={q}
            onInput={(e) => setQ((e.target as HTMLInputElement).value)}
            placeholder="Search endpoints…"
            class="ui-input mt-0 font-mono text-[11px] font-bold"
          />
        </div>
        <div class="max-h-64 overflow-y-auto p-2">
          {filtered.length ? (
            <div class="space-y-1">
              {filtered.map((e) => {
                const g = groupForEndpoint(e);
                const cls =
                  g === "local"
                    ? "ui-endpoint-opt-local"
                    : "ui-endpoint-opt-remote";
                return (
                  <button
                    type="button"
                    key={e.id}
                    class={`w-full rounded-[var(--radius-sm)] border-2 p-2 text-left font-mono ${cls}`}
                    onClick={(ev) => {
                      ev.preventDefault();
                      onChange(e.id);
                      // close details
                      const d = (ev.currentTarget as HTMLElement).closest("details") as
                        | HTMLDetailsElement
                        | null;
                      if (d) d.open = false;
                      setQ("");
                    }}
                    title={e.id}
                  >
                    <div class="flex flex-wrap items-center justify-between gap-2">
                      <div class="min-w-0">
                        <div class="truncate text-xs font-black">{e.label}</div>
                        <div class="truncate text-[10px] font-bold opacity-80">{e.sub}</div>
                      </div>
                      <div class="shrink-0 text-right">
                        <div class="text-[10px] font-black uppercase">{e.kind}</div>
                        <div class="text-[10px] opacity-80">{e.id}</div>
                      </div>
                    </div>
                  </button>
                );
              })}
            </div>
          ) : (
            <div class="font-mono text-[11px] ui-text-subtle">No matches.</div>
          )}
        </div>
      </div>
    </details>
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
  const [highlightIds, setHighlightIds] = useState<Set<string>>(() => new Set());
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

  const filtered = useMemo(() => {
    const list = endpoints.filter((e) => {
      const g = groupForEndpoint(e);
      if (g === "local" && !showLocal) return false;
      if (g === "remote" && !showRemote) return false;
      const conn = e.peerId !== undefined ? (isPeerConnected.get(e.peerId) ?? false) : false;
      if (connectedOnly && !conn) return false;
      if (!matchesQuery(e, query)) return false;
      return true;
    });

    const activity = (e: Endpoint): number => {
      if (e.peerId === undefined) return -1;
      const p = byPeerId.get(e.peerId);
      return p ? p.recv + p.sent : -1;
    };
    const connFlag = (e: Endpoint): number =>
      e.peerId !== undefined && (isPeerConnected.get(e.peerId) ?? false) ? 1 : 0;

    return [...list].sort((a, b) => {
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
      // activity
      return activity(b) - activity(a) || a.label.localeCompare(b.label);
    });
  }, [
    endpoints,
    showLocal,
    showRemote,
    connectedOnly,
    query,
    sortKey,
    isPeerConnected,
    byPeerId,
  ]);

  const [targetByEndpoint, setTargetByEndpoint] = useState<Map<string, string>>(
    () => new Map(),
  );

  const getTarget = (fromId: string): string | null => {
    const v = targetByEndpoint.get(fromId);
    if (typeof v === "string" && v) return v;
    const firstOther = endpoints.find((e) => e.id !== fromId);
    return firstOther?.id ?? null;
  };

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
            <Toggle label="Local" value={showLocal} onChange={setShowLocal} />
            <Toggle label="Remote" value={showRemote} onChange={setShowRemote} />
            <Toggle
              label="Connected"
              value={connectedOnly}
              onChange={setConnectedOnly}
            />
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
        <div class="mt-3 flex flex-wrap items-center gap-2 font-mono text-[10px] ui-text-muted">
          <span>
            Showing{" "}
            <span class="font-black ui-text">
              {shown.length}
            </span>{" "}
            / {endpoints.length} endpoint(s)
          </span>
          <span class="ui-text-subtle">·</span>
          <span class="inline-flex items-center gap-1">
            <AccentPill group="local" connected={true}>
              local = green
            </AccentPill>
            <AccentPill group="remote" connected={true}>
              remote = blue
            </AccentPill>
          </span>
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
          const ring = highlightIds.has(e.id) ? " ui-tr-highlight" : "";
          const surf = conn
            ? "bg-[color:var(--color-surface)]"
            : "bg-[color:var(--color-surface-zebra-b)]";
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
          const connectedEndpoints = Array.from(new Set([...outPeerIds, ...inPeerIds]))
            .map((x) => endpointByPeerId.get(x))
            .filter((x): x is Endpoint => !!x)
            .map((ce) => {
              const otherPeerId = ce.peerId;
              const hasOut = otherPeerId !== undefined ? outSet.has(otherPeerId) : false;
              const hasIn = otherPeerId !== undefined ? inSet.has(otherPeerId) : false;
              const dir = hasIn && hasOut ? "IN-OUT" : hasOut ? "OUT" : hasIn ? "IN" : "";
              return { ce, dir };
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

          const targetId = getTarget(e.id);

          return (
            <div
              key={e.id}
              ref={(el) => {
                if (el) cardRefs.current.set(e.id, el);
                else cardRefs.current.delete(e.id);
              }}
              class={`overflow-hidden rounded-[var(--radius-md)] border-2 ui-shadow-card ${surf} ${accentBorder}${ring}`}
            >
              <div class="ui-peer-card-head px-3 py-2">
                <div class="flex flex-wrap items-center justify-between gap-2">
                  <div class="flex min-w-0 flex-wrap items-center gap-2">
                    <span class="min-w-0 truncate font-mono text-sm font-black ui-text">
                      {e.label}
                    </span>
                    <AccentPill group={g} connected={conn}>
                      {g === "local" ? "LOCAL" : "REMOTE"}
                    </AccentPill>
                    <span class="truncate font-mono text-[10px] font-bold uppercase tracking-wide ui-text-muted">
                      {e.kind}
                    </span>
                    {pid !== undefined ? (
                      <span class="ui-peer-cap">
                        peer #{pid}
                      </span>
                    ) : null}
                  </div>
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
                </div>
                <div class="mt-1 flex flex-wrap items-center gap-2 font-mono text-[11px] ui-text-muted">
                  <span class="font-bold uppercase ui-text-subtle">
                    {conn ? "connected" : "disconnected"}
                  </span>
                  <span class="ui-text-subtle">·</span>
                  <span class="truncate">{e.sub}</span>
                </div>
              </div>

              <div class="p-3">
                <div class="grid gap-3 md:grid-cols-[1fr_auto]">
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

                  <div class="w-full max-w-[22rem]">
                    <div class="ui-panel-soft p-2">
                      <div class="mb-2 font-mono text-[10px] font-black uppercase ui-text-muted">
                        Connect
                      </div>
                      <div class="flex flex-col gap-2">
                        <FancyEndpointSelect
                          selfId={e.id}
                          endpoints={endpoints}
                          value={targetId}
                          onChange={(v) => {
                            setTargetByEndpoint((prev) => {
                              const n = new Map(prev);
                              n.set(e.id, v);
                              return n;
                            });
                          }}
                        />
                        <div class="flex flex-wrap gap-2">
                          <Button
                            disabled={targetId === null || targetId === e.id || endpoints.length <= 1}
                            onClick={() => {
                              if (!targetId) return;
                              void doConnect(e.id, targetId);
                            }}
                          >
                            connect ↔
                          </Button>
                        </div>
                        <div class="font-mono text-[10px] ui-text-muted">
                          server decides ALSA aconnect vs router routing
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          );
        })}
      </div>

      <p class="font-mono text-[10px] ui-text-subtle">
        Connected = has any router edge in or out (only for endpoints currently backed
        by a router peer) or an ALSA subscription (ALSA↔ALSA). IN/OUT LEDs light when
        the matched peer recv/sent counters increased since the previous poll.
      </p>
    </div>
  );
}

