import { useEffect, useMemo, useRef, useState } from "preact/hooks";
import type { RouterPeer } from "../model";
import { buildRecvFromMap } from "../model";
import { peerCombinedLatencyMs, PeerLatencyHoverCell } from "./LatencyBar";

type SortKey =
  | "id"
  | "name"
  | "type"
  | "recv"
  | "sent"
  | "events"
  | "send_n"
  | "lat_sum";

function sortPeers(peers: RouterPeer[], key: SortKey, asc: boolean): RouterPeer[] {
  const dir = asc ? 1 : -1;
  const val = (p: RouterPeer): number | string => {
    switch (key) {
      case "id":
        return p.id;
      case "name":
        return p.name.toLowerCase();
      case "type":
        return p.type.toLowerCase();
      case "recv":
        return p.recv;
      case "sent":
        return p.sent;
      case "events":
        return p.recv + p.sent;
      case "send_n":
        return p.send_to.length;
      case "lat_sum": {
        const c = peerCombinedLatencyMs(p);
        return c === null ? -1 : c;
      }
      default:
        return 0;
    }
  };
  return [...peers].sort((a, b) => {
    const va = val(a);
    const vb = val(b);
    if (typeof va === "string" && typeof vb === "string") {
      return va < vb ? -dir : va > vb ? dir : 0;
    }
    const na = Number(va);
    const nb = Number(vb);
    return na < nb ? -dir : na > nb ? dir : 0;
  });
}

function Th({
  label,
  active,
  asc,
  onClick,
}: {
  label: string;
  active: boolean;
  asc: boolean;
  onClick: () => void;
}) {
  return (
    <th class="border-b-2 border-zinc-900 bg-zinc-200 px-2 py-2 text-left dark:border-zinc-100 dark:bg-zinc-800">
      <button
        type="button"
        class={`font-mono text-xs font-bold uppercase tracking-wide hover:underline ${active ? "text-amber-800 dark:text-amber-300" : ""}`}
        onClick={onClick}
      >
        {label}
        {active ? (asc ? " ▲" : " ▼") : ""}
      </button>
    </th>
  );
}

type Props = { peers: RouterPeer[]; highlightPeerId?: number | null };

export function PeersTable({ peers, highlightPeerId }: Props) {
  const [sortKey, setSortKey] = useState<SortKey>("events");
  const [sortAsc, setSortAsc] = useState(false);
  const rowRefs = useRef<Map<number, HTMLTableRowElement>>(new Map());

  const recvFrom = useMemo(() => buildRecvFromMap(peers), [peers]);
  const byId = useMemo(() => new Map(peers.map((p) => [p.id, p])), [peers]);

  const sorted = useMemo(
    () => sortPeers(peers, sortKey, sortAsc),
    [peers, sortKey, sortAsc],
  );

  useEffect(() => {
    if (highlightPeerId === undefined || highlightPeerId === null) return;
    const el = rowRefs.current.get(highlightPeerId);
    el?.scrollIntoView({ block: "nearest", behavior: "smooth" });
  }, [highlightPeerId, sorted]);

  const toggle = (k: SortKey) => {
    if (sortKey === k) setSortAsc(!sortAsc);
    else {
      setSortKey(k);
      setSortAsc(k === "name" || k === "type");
    }
  };

  const fmtFrom = (ids: number[] | undefined) => {
    if (!ids?.length) return "—";
    return ids
      .map((id) => {
        const q = byId.get(id);
        return `#${id}${q?.name ? ` ${q.name}` : ""}`;
      })
      .join(", ");
  };

  return (
    <div class="overflow-x-auto overflow-y-visible">
      <table class="w-full min-w-[56rem] border-collapse border-2 border-zinc-900 text-left dark:border-zinc-100">
        <thead>
          <tr>
            <Th
              label="ID"
              active={sortKey === "id"}
              asc={sortAsc}
              onClick={() => toggle("id")}
            />
            <Th
              label="Name"
              active={sortKey === "name"}
              asc={sortAsc}
              onClick={() => toggle("name")}
            />
            <Th
              label="Type"
              active={sortKey === "type"}
              asc={sortAsc}
              onClick={() => toggle("type")}
            />
            <Th
              label="Recv"
              active={sortKey === "recv"}
              asc={sortAsc}
              onClick={() => toggle("recv")}
            />
            <Th
              label="Sent"
              active={sortKey === "sent"}
              asc={sortAsc}
              onClick={() => toggle("sent")}
            />
            <Th
              label="Events Σ"
              active={sortKey === "events"}
              asc={sortAsc}
              onClick={() => toggle("events")}
            />
            <Th
              label="→#"
              active={sortKey === "send_n"}
              asc={sortAsc}
              onClick={() => toggle("send_n")}
            />
            <th class="border-b-2 border-zinc-900 bg-zinc-200 px-2 py-2 font-mono text-xs font-bold uppercase dark:border-zinc-100 dark:bg-zinc-800">
              → to
            </th>
            <th class="border-b-2 border-zinc-900 bg-zinc-200 px-2 py-2 font-mono text-xs font-bold uppercase dark:border-zinc-100 dark:bg-zinc-800">
              ← from
            </th>
            <Th
              label="Latency"
              active={sortKey === "lat_sum"}
              asc={sortAsc}
              onClick={() => toggle("lat_sum")}
            />
          </tr>
        </thead>
        <tbody>
          {sorted.map((p) => (
            <tr
              key={p.id}
              ref={(el) => {
                if (el) rowRefs.current.set(p.id, el);
                else rowRefs.current.delete(p.id);
              }}
              class={`border-b border-zinc-300 odd:bg-white even:bg-zinc-50 dark:border-zinc-700 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80 ${
                highlightPeerId === p.id
                  ? "ring-2 ring-inset ring-amber-500 dark:ring-amber-400"
                  : ""
              }`}
            >
              <td class="px-2 py-1.5 font-mono text-sm font-bold tabular-nums">
                {p.id}
              </td>
              <td class="max-w-[12rem] truncate px-2 py-1.5 font-mono text-sm">
                {p.name || "—"}
              </td>
              <td class="max-w-[10rem] truncate px-2 py-1.5 font-mono text-xs text-zinc-600 dark:text-zinc-400">
                {p.type}
              </td>
              <td class="px-2 py-1.5 font-mono text-sm tabular-nums">{p.recv}</td>
              <td class="px-2 py-1.5 font-mono text-sm tabular-nums">{p.sent}</td>
              <td class="px-2 py-1.5 font-mono text-sm font-semibold tabular-nums">
                {p.recv + p.sent}
              </td>
              <td class="px-2 py-1.5 font-mono text-sm tabular-nums">
                {p.send_to.length}
              </td>
              <td class="max-w-[11rem] truncate px-2 py-1.5 font-mono text-[11px]">
                {p.send_to.length ? p.send_to.join(", ") : "—"}
              </td>
              <td class="max-w-[11rem] truncate px-2 py-1.5 font-mono text-[11px]">
                {fmtFrom(recvFrom.get(p.id))}
              </td>
              <td class="relative min-w-[10rem] overflow-visible px-1 py-1 align-top">
                <PeerLatencyHoverCell peer={p} />
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      <p class="mt-2 font-mono text-[10px] text-zinc-500">
        Events Σ = recv + sent (packet counters). Latency bar = sum of available
        last-sample latencies; hover for breakdown. Scale:{" "}
        <code class="rounded bg-zinc-200 px-0.5 dark:bg-zinc-800">latencyScale.ts</code>.
      </p>
    </div>
  );
}
