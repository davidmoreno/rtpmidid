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
      ? "border-emerald-900 bg-emerald-50 text-emerald-950 dark:border-emerald-200 dark:bg-emerald-500/10 dark:text-emerald-200"
      : "border-sky-900 bg-sky-50 text-sky-950 dark:border-sky-200 dark:bg-sky-500/10 dark:text-sky-200";
  return (
    <span class={`inline-flex items-center gap-1 rounded border-2 px-1 py-0.5 font-mono text-[11px] font-bold ${cls}`}>
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
      <summary class="list-none cursor-pointer border-2 border-zinc-900 bg-white px-2 py-1 font-mono text-xs font-black shadow-[3px_3px_0_0_#18181b] hover:bg-zinc-100 hover:ring-2 hover:ring-inset hover:ring-zinc-900 dark:border-zinc-100 dark:bg-zinc-950 dark:shadow-[3px_3px_0_0_#fafafa] dark:hover:bg-zinc-900 dark:hover:ring-zinc-100">
        <span class="flex items-center justify-between gap-2">
          <span class="min-w-0">
            {selected ? (
              <SelectBadge e={selected} />
            ) : (
              <span class="text-zinc-600 dark:text-zinc-400">Choose endpoint…</span>
            )}
          </span>
          <span class="font-black text-zinc-700 dark:text-zinc-300">▾</span>
        </span>
      </summary>

      <div class="absolute z-50 mt-1 w-full border-2 border-zinc-900 bg-white shadow-[6px_6px_0_0_#18181b] dark:border-zinc-100 dark:bg-zinc-950 dark:shadow-[6px_6px_0_0_#fafafa]">
        <div class="border-b-2 border-zinc-900 bg-zinc-200 p-2 dark:border-zinc-100 dark:bg-zinc-800">
          <input
            value={q}
            onInput={(e) => setQ((e.target as HTMLInputElement).value)}
            placeholder="Search endpoints…"
            class="w-full border-2 border-zinc-900 bg-white px-2 py-1 font-mono text-[11px] font-bold dark:border-zinc-100 dark:bg-zinc-950"
          />
        </div>
        <div class="max-h-64 overflow-y-auto p-2">
          {filtered.length ? (
            <div class="space-y-1">
              {filtered.map((e) => {
                const g = groupForEndpoint(e);
                const cls =
                  g === "local"
                    ? "border-emerald-900 bg-emerald-50 text-emerald-950 hover:bg-emerald-100 dark:border-emerald-200 dark:bg-emerald-500/10 dark:text-emerald-200 dark:hover:bg-emerald-500/15"
                    : "border-sky-900 bg-sky-50 text-sky-950 hover:bg-sky-100 dark:border-sky-200 dark:bg-sky-500/10 dark:text-sky-200 dark:hover:bg-sky-500/15";
                return (
                  <button
                    type="button"
                    key={e.id}
                    class={`w-full border-2 p-2 text-left font-mono ${cls}`}
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
            <div class="font-mono text-[11px] text-zinc-500">No matches.</div>
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
      ? connected
        ? `${base} border-emerald-900 bg-emerald-200 text-emerald-950 dark:border-emerald-200 dark:bg-emerald-500/25 dark:text-emerald-200`
        : `${base} border-emerald-900 bg-emerald-100 text-emerald-950 dark:border-emerald-200 dark:bg-emerald-500/15 dark:text-emerald-200`
      : connected
        ? `${base} border-sky-900 bg-sky-200 text-sky-950 dark:border-sky-200 dark:bg-sky-500/25 dark:text-sky-200`
        : `${base} border-sky-900 bg-sky-100 text-sky-950 dark:border-sky-200 dark:bg-sky-500/15 dark:text-sky-200`;
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
    group === "local"
      ? "border-emerald-700 bg-emerald-400 text-zinc-900 shadow-[0_0_14px_rgba(52,211,153,0.9)] dark:border-emerald-300 dark:bg-emerald-400"
      : "border-sky-700 bg-sky-400 text-zinc-900 shadow-[0_0_14px_rgba(56,189,248,0.9)] dark:border-sky-300 dark:bg-sky-400";
  const unlit =
    "border-zinc-400 bg-zinc-100 text-zinc-600 dark:border-zinc-600 dark:bg-zinc-800 dark:text-zinc-400";
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
      class={`border-2 px-2 py-1 font-mono text-[11px] font-black uppercase shadow-[3px_3px_0_0_#18181b] dark:shadow-[3px_3px_0_0_#fafafa] ${
        value
          ? "border-zinc-900 bg-amber-300 text-zinc-950 dark:border-zinc-100 dark:bg-amber-600 dark:text-zinc-950"
          : "border-zinc-500 bg-zinc-100 text-zinc-700 dark:border-zinc-600 dark:bg-zinc-900 dark:text-zinc-300"
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
      <section class="border-2 border-zinc-900 bg-white p-3 shadow-[6px_6px_0_0_#18181b] dark:border-zinc-100 dark:bg-zinc-900 dark:shadow-[6px_6px_0_0_#fafafa]">
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
              <span class="font-mono text-[11px] font-bold uppercase text-zinc-600 dark:text-zinc-400">
                Sort
              </span>
              <select
                class="border-2 border-zinc-900 bg-white px-2 py-1 font-mono text-[11px] font-bold dark:border-zinc-100 dark:bg-zinc-950"
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
          <label class="min-w-[14rem] grow font-mono text-[11px] font-bold uppercase text-zinc-600 dark:text-zinc-400">
            Search
            <input
              class="mt-1 w-full border-2 border-zinc-900 bg-white px-2 py-1 font-mono text-xs dark:border-zinc-100 dark:bg-zinc-950"
              value={query}
              onInput={(e) => setQuery((e.target as HTMLInputElement).value)}
              placeholder="name, device, host, kind…"
            />
          </label>
        </div>
        <div class="mt-3 flex flex-wrap items-center gap-2 font-mono text-[10px] text-zinc-600 dark:text-zinc-400">
          <span>
            Showing{" "}
            <span class="font-black text-zinc-900 dark:text-zinc-100">
              {shown.length}
            </span>{" "}
            / {endpoints.length} endpoint(s)
          </span>
          <span class="text-zinc-400">·</span>
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
          const ring = highlightIds.has(e.id)
            ? " ring-2 ring-inset ring-amber-500 dark:ring-amber-400"
            : "";
          const base = conn
            ? "border-2 bg-white shadow-[6px_6px_0_0_#18181b] dark:bg-zinc-900 dark:shadow-[6px_6px_0_0_#fafafa]"
            : "border-2 bg-zinc-100 shadow-[6px_6px_0_0_#18181b] dark:bg-zinc-950 dark:shadow-[6px_6px_0_0_#fafafa]";
          const accentBorder =
            g === "local"
              ? conn
                ? "border-emerald-900 dark:border-emerald-200"
                : "border-emerald-950 dark:border-emerald-300"
              : conn
                ? "border-sky-900 dark:border-sky-200"
                : "border-sky-950 dark:border-sky-300";

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
              class={`${base} ${accentBorder}${ring}`}
            >
              <div class="border-b-2 border-zinc-900 bg-zinc-200 px-3 py-2 dark:border-zinc-100 dark:bg-zinc-800">
                <div class="flex flex-wrap items-center justify-between gap-2">
                  <div class="flex min-w-0 flex-wrap items-center gap-2">
                    <span class="min-w-0 truncate font-mono text-sm font-black text-zinc-950 dark:text-zinc-50">
                      {e.label}
                    </span>
                    <AccentPill group={g} connected={conn}>
                      {g === "local" ? "LOCAL" : "REMOTE"}
                    </AccentPill>
                    <span class="truncate font-mono text-[10px] font-bold uppercase tracking-wide text-zinc-600 dark:text-zinc-300">
                      {e.kind}
                    </span>
                    {pid !== undefined ? (
                      <span class="rounded border border-zinc-400 bg-zinc-100 px-1 py-0.5 font-mono text-[10px] font-black uppercase text-zinc-700 dark:border-zinc-600 dark:bg-zinc-900 dark:text-zinc-300">
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
                <div class="mt-1 flex flex-wrap items-center gap-2 font-mono text-[11px] text-zinc-700 dark:text-zinc-300">
                  <span class="font-bold uppercase text-zinc-500 dark:text-zinc-400">
                    {conn ? "connected" : "disconnected"}
                  </span>
                  <span class="text-zinc-400">·</span>
                  <span class="truncate">{e.sub}</span>
                </div>
              </div>

              <div class="p-3">
                <div class="grid gap-3 md:grid-cols-[1fr_auto]">
                  <div class="space-y-2">
                    <div class="flex flex-wrap items-center gap-2 font-mono text-xs">
                      <span class="font-black uppercase text-zinc-600 dark:text-zinc-400">
                        Stats
                      </span>
                      <span class="text-zinc-400">·</span>
                      {peer ? (
                        <>
                          <span class="tabular-nums">
                            recv{" "}
                            <strong class="text-zinc-900 dark:text-zinc-100">
                              {peer.recv}
                            </strong>
                          </span>
                          <span class="tabular-nums">
                            sent{" "}
                            <strong class="text-zinc-900 dark:text-zinc-100">
                              {peer.sent}
                            </strong>
                          </span>
                          <span class="tabular-nums">
                            Σ{" "}
                            <strong class="text-zinc-900 dark:text-zinc-100">
                              {peer.recv + peer.sent}
                            </strong>
                          </span>
                          {peerCombinedLatencyMs(peer) !== null ? (
                            <>
                              <span class="text-zinc-400">·</span>
                              <span class="inline-flex items-center gap-2">
                                <span class="font-black uppercase text-zinc-600 dark:text-zinc-400">
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
                        <span class="text-zinc-500">—</span>
                      )}
                    </div>

                    <div>
                      <div class="mb-1 font-mono text-[10px] font-black uppercase text-zinc-600 dark:text-zinc-400">
                        connected to ({connectedEndpoints.length}{e.kind === "alsa_seq" ? ` + ${alsaOut.length + alsaIn.length} alsa` : ""})
                      </div>
                      <div class="flex flex-wrap gap-1">
                        {connectedEndpoints.length ? (
                          connectedEndpoints.map(({ ce, dir }) => (
                            <span
                              key={ce.id}
                              class={`inline-flex items-center gap-1 rounded border-2 px-1 py-0.5 font-mono text-[11px] font-bold ${
                                groupForEndpoint(ce) === "local"
                                  ? "border-emerald-900 bg-emerald-50 text-emerald-950 dark:border-emerald-200 dark:bg-emerald-500/10 dark:text-emerald-200"
                                  : "border-sky-900 bg-sky-50 text-sky-950 dark:border-sky-200 dark:bg-sky-500/10 dark:text-sky-200"
                              }`}
                              title={ce.id}
                            >
                              <button
                                type="button"
                                class={`hover:underline ${
                                  groupForEndpoint(ce) === "local"
                                    ? "hover:text-emerald-950 dark:hover:text-emerald-200"
                                    : "hover:text-sky-950 dark:hover:text-sky-200"
                                }`}
                                onClick={() => pulseEndpoints([e.id, ce.id], ce.id)}
                              >
                                {ce.label}
                              </button>
                              {dir ? (
                                <span class="rounded border border-zinc-400 bg-white px-1 text-[9px] font-black uppercase text-zinc-700 dark:border-zinc-600 dark:bg-zinc-950 dark:text-zinc-300">
                                  {dir}
                                </span>
                              ) : null}
                              <button
                                type="button"
                                class={`rounded border px-1 text-[10px] font-black uppercase hover:bg-white dark:hover:bg-zinc-950 ${
                                  groupForEndpoint(ce) === "local"
                                    ? "border-emerald-900 bg-emerald-200 text-emerald-950 dark:border-emerald-200 dark:bg-emerald-500/25 dark:text-emerald-200"
                                    : "border-sky-900 bg-sky-200 text-sky-950 dark:border-sky-200 dark:bg-sky-500/25 dark:text-sky-200"
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
                                  class="inline-flex items-center gap-1 rounded border-2 border-emerald-900 bg-emerald-50 px-1 py-0.5 font-mono text-[11px] font-bold text-emerald-950 dark:border-emerald-200 dark:bg-emerald-500/10 dark:text-emerald-200"
                                  title={`${fromId} -> ${toId}`}
                                >
                                  <button
                                    type="button"
                                    class="hover:underline"
                                    onClick={() => pulseEndpoints([e.id, toId], toId)}
                                  >
                                    {toLabel}
                                  </button>
                                  <span class="font-black">→</span>
                                  <button
                                    type="button"
                                    class="rounded border border-emerald-900 bg-emerald-200 px-1 text-[10px] font-black uppercase text-emerald-950 hover:bg-emerald-100 dark:border-emerald-200 dark:bg-emerald-500/25 dark:text-emerald-200 dark:hover:bg-emerald-500/15"
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
                                  class="inline-flex items-center gap-1 rounded border-2 border-emerald-900 bg-emerald-50 px-1 py-0.5 font-mono text-[11px] font-bold text-emerald-950 dark:border-emerald-200 dark:bg-emerald-500/10 dark:text-emerald-200"
                                  title={`${fromId} -> ${toId}`}
                                >
                                  <button
                                    type="button"
                                    class="hover:underline"
                                    onClick={() => pulseEndpoints([e.id, fromId], fromId)}
                                  >
                                    {fromLabel}
                                  </button>
                                  <span class="font-black">→</span>
                                  <span class="text-zinc-600 dark:text-zinc-400">
                                    this
                                  </span>
                                  <button
                                    type="button"
                                    class="rounded border border-emerald-900 bg-emerald-200 px-1 text-[10px] font-black uppercase text-emerald-950 hover:bg-emerald-100 dark:border-emerald-200 dark:bg-emerald-500/25 dark:text-emerald-200 dark:hover:bg-emerald-500/15"
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
                          <span class="font-mono text-[11px] text-zinc-500">—</span>
                        ) : null}
                      </div>
                    </div>
                  </div>

                  <div class="w-full max-w-[22rem]">
                    <div class="border-2 border-zinc-900 bg-zinc-50 p-2 dark:border-zinc-100 dark:bg-zinc-950">
                      <div class="mb-2 font-mono text-[10px] font-black uppercase text-zinc-600 dark:text-zinc-400">
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
                        <div class="font-mono text-[10px] text-zinc-600 dark:text-zinc-400">
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

      <p class="font-mono text-[10px] text-zinc-500">
        Connected = has any router edge in or out (only for endpoints currently backed
        by a router peer) or an ALSA subscription (ALSA↔ALSA). IN/OUT LEDs light when
        the matched peer recv/sent counters increased since the previous poll.
      </p>
    </div>
  );
}

