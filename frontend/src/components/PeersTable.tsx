import { useMemo, useState } from "preact/hooks";
import type { RouterPeer } from "../model";
import { LatencyBar } from "./LatencyBar";

type SortKey =
  | "id"
  | "name"
  | "type"
  | "recv"
  | "sent"
  | "total"
  | "send_n"
  | "int_u"
  | "net_l";

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
      case "total":
        return p.recv + p.sent;
      case "send_n":
        return p.send_to.length;
      case "int_u":
        return p.internal?.until?.last ?? -1;
      case "net_l":
        return p.network?.last ?? -1;
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

type Props = { peers: RouterPeer[] };

export function PeersTable({ peers }: Props) {
  const [sortKey, setSortKey] = useState<SortKey>("total");
  const [sortAsc, setSortAsc] = useState(false);

  const sorted = useMemo(
    () => sortPeers(peers, sortKey, sortAsc),
    [peers, sortKey, sortAsc],
  );

  const toggle = (k: SortKey) => {
    if (sortKey === k) setSortAsc(!sortAsc);
    else {
      setSortKey(k);
      setSortAsc(k === "name" || k === "type");
    }
  };

  return (
    <div class="overflow-x-auto">
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
              label="Total"
              active={sortKey === "total"}
              asc={sortAsc}
              onClick={() => toggle("total")}
            />
            <Th
              label="→#"
              active={sortKey === "send_n"}
              asc={sortAsc}
              onClick={() => toggle("send_n")}
            />
            <th class="border-b-2 border-zinc-900 bg-zinc-200 px-2 py-2 font-mono text-xs font-bold uppercase dark:border-zinc-100 dark:bg-zinc-800">
              send_to
            </th>
            <Th
              label="IntΔ"
              active={sortKey === "int_u"}
              asc={sortAsc}
              onClick={() => toggle("int_u")}
            />
            <Th
              label="RTP ms"
              active={sortKey === "net_l"}
              asc={sortAsc}
              onClick={() => toggle("net_l")}
            />
          </tr>
        </thead>
        <tbody>
          {sorted.map((p) => (
            <tr
              key={p.id}
              class="border-b border-zinc-300 odd:bg-white even:bg-zinc-50 dark:border-zinc-700 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80"
            >
              <td class="px-2 py-1.5 font-mono text-sm font-bold tabular-nums">
                {p.id}
              </td>
              <td class="max-w-[14rem] truncate px-2 py-1.5 font-mono text-sm">
                {p.name || "—"}
              </td>
              <td class="px-2 py-1.5 font-mono text-xs text-zinc-600 dark:text-zinc-400">
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
              <td class="max-w-[10rem] truncate px-2 py-1.5 font-mono text-xs">
                {p.send_to.length ? p.send_to.join(", ") : "—"}
              </td>
              <td class="min-w-[7rem] px-2 py-1 font-mono text-xs tabular-nums">
                {p.internal?.until?.last !== undefined
                  ? `${p.internal.until.last.toFixed(3)}`
                  : "—"}
              </td>
              <td class="min-w-[6rem] px-2 py-1 font-mono text-xs tabular-nums">
                {p.network?.last !== undefined
                  ? `${p.network.last.toFixed(2)}`
                  : "—"}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      <p class="mt-2 font-mono text-[10px] text-zinc-500">
        Total = recv + sent (sort default: most active). IntΔ = internal queue→
        send_midi (ms). RTP ms = CK latency when available.
      </p>
    </div>
  );
}

export function PeerLatencyPanel({ peer }: { peer: RouterPeer }) {
  return (
    <div class="border-2 border-zinc-800 p-3 dark:border-zinc-200">
      <div class="mb-2 font-mono text-sm font-bold">
        #{peer.id} {peer.name || "(unnamed)"}
      </div>
      <div class="grid gap-3 sm:grid-cols-2">
        <div>
          <div class="mb-1 font-mono text-[10px] font-bold uppercase text-zinc-500">
            Internal (this peer)
          </div>
          <LatencyBar
            label="Until send_midi (last)"
            triple={peer.internal?.until}
            capMs={25}
          />
          <LatencyBar
            label="send_midi() (last)"
            triple={peer.internal?.sendMidi}
            capMs={10}
          />
        </div>
        <div>
          <div class="mb-1 font-mono text-[10px] font-bold uppercase text-zinc-500">
            Network RTP (if any)
          </div>
          {peer.network ? (
            <LatencyBar label="CK latency (last)" triple={peer.network} capMs={150} />
          ) : (
            <p class="font-mono text-xs text-zinc-500">No RTP latency on this peer.</p>
          )}
        </div>
      </div>
    </div>
  );
}
