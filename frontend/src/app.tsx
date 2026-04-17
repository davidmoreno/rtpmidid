import { useCallback, useEffect, useMemo, useState } from "preact/hooks";
import { Button } from "./components/Button";
import { Card } from "./components/Card";
import { ConnectionsTable } from "./components/ConnectionsTable";
import { EdgesTable } from "./components/EdgesTable";
import { MdnsTables } from "./components/MdnsTables";
import { PeerLatencyPanel, PeersTable } from "./components/PeersTable";
import { Tabs, type TabDef } from "./components/Tabs";
import { buildConnections, buildEdges, normalizePeers, parseMdns } from "./model";
import { RpcClient } from "./rpc";

type StatusResult = {
  version?: string;
  router?: unknown[];
  mdns?: Record<string, unknown>;
  settings?: Record<string, unknown>;
};

const AUTO_TABS = new Set(["overview", "stats", "connections"]);

function useTheme() {
  const [dark, setDark] = useState(() => {
    const s = localStorage.getItem("rtpmidid-theme");
    if (s === "dark") return true;
    if (s === "light") return false;
    return window.matchMedia("(prefers-color-scheme: dark)").matches;
  });
  useEffect(() => {
    document.documentElement.classList.toggle("dark", dark);
    localStorage.setItem("rtpmidid-theme", dark ? "dark" : "light");
  }, [dark]);
  return { dark, setDark };
}

function statTable(rows: { k: string; v: string }[]) {
  return (
    <table class="w-full border-collapse font-mono text-sm">
      <tbody>
        {rows.map((r) => (
          <tr key={r.k} class="border-b border-zinc-300 dark:border-zinc-700">
            <th class="py-1 pr-4 text-left font-bold">{r.k}</th>
            <td class="py-1 text-zinc-700 dark:text-zinc-300">{r.v}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

export function App() {
  const { dark, setDark } = useTheme();
  const [status, setStatus] = useState<string>("");
  const [data, setData] = useState<StatusResult | null>(null);
  const [tab, setTab] = useState("overview");
  const [lastRefresh, setLastRefresh] = useState<Date | null>(null);
  const [authUser, setAuthUser] = useState("");
  const [authPass, setAuthPass] = useState("");
  const [connectHost, setConnectHost] = useState("");
  const [connectPort, setConnectPort] = useState("5004");
  const [connectName, setConnectName] = useState("");
  const [createJson, setCreateJson] = useState(
    '{"type":"network_rtpmidi_client_t","name":"Remote","hostname":"127.0.0.1","port":5004}',
  );
  const [fromId, setFromId] = useState("");
  const [toId, setToId] = useState("");

  const rpc = useMemo(
    () =>
      new RpcClient(
        (m) => setStatus(m),
        (ev) => console.debug("event", ev),
      ),
    [],
  );

  const refresh = useCallback(async () => {
    try {
      const r = (await rpc.call("status", {})) as StatusResult;
      setData(r);
      setLastRefresh(new Date());
    } catch (e) {
      setStatus(String(e));
    }
  }, [rpc]);

  useEffect(() => {
    rpc.setAuth(authUser, authPass);
    rpc
      .connect()
      .then(() => refresh())
      .catch((e) => setStatus(String(e)));
    return () => rpc.disconnect();
  }, []);

  useEffect(() => {
    if (!AUTO_TABS.has(tab)) return undefined;
    const t = window.setInterval(() => {
      void refresh();
    }, 5000);
    return () => window.clearInterval(t);
  }, [tab, refresh]);

  const routerRaw = data?.router ?? [];
  const peers = useMemo(() => normalizePeers(routerRaw), [routerRaw]);
  const edges = useMemo(() => buildEdges(peers), [peers]);
  const connections = useMemo(() => buildConnections(peers), [peers]);
  const mdnsParsed = useMemo(
    () => parseMdns(data?.mdns as Record<string, unknown> | undefined),
    [data?.mdns],
  );

  const statsRows = useMemo(() => {
    const edgesN = edges.length;
    let rtpPeers = 0;
    for (const p of peers) {
      const raw = p.raw.peers as unknown[] | undefined;
      if (raw?.length) rtpPeers += raw.length;
      if (p.raw.peer) rtpPeers += 1;
    }
    return [
      { k: "Router peers", v: String(peers.length) },
      { k: "Routing edges", v: String(edgesN) },
      { k: "RTP sub-peer rows (approx)", v: String(rtpPeers) },
      { k: "Version", v: String(data?.version ?? "—") },
    ];
  }, [data?.version, peers, edges.length]);

  const overviewContent = (
    <div class="space-y-4">
      <div class="flex flex-wrap items-center justify-between gap-2 font-mono text-xs text-zinc-600 dark:text-zinc-400">
        <span>
          Auto-refresh every <strong class="text-zinc-900 dark:text-zinc-100">5s</strong> on this tab.
        </span>
        {lastRefresh && (
          <span class="tabular-nums">
            Last update: {lastRefresh.toLocaleTimeString()}
          </span>
        )}
      </div>
      <Card title="Peers & activity">
        <PeersTable peers={peers} />
      </Card>
      <Card title="Connections (router edges)">
        <EdgesTable edges={edges} />
      </Card>
      <Card title="Discovery (mDNS)">
        <MdnsTables
          status={mdnsParsed.status}
          announcements={mdnsParsed.announcements}
          remotes={mdnsParsed.remotes}
        />
      </Card>
    </div>
  );

  const connectionsContent = (
    <div class="space-y-4">
      <div class="flex flex-wrap items-center justify-between gap-2 font-mono text-xs text-zinc-600 dark:text-zinc-400">
        <span>
          Auto-refresh every <strong class="text-zinc-900 dark:text-zinc-100">5s</strong>{" "}
          on this tab.
        </span>
        {lastRefresh && (
          <span class="tabular-nums">
            Last update: {lastRefresh.toLocaleTimeString()}
          </span>
        )}
      </div>
      <Card title="Connections (router + RTP)">
        <ConnectionsTable rows={connections} />
      </Card>
    </div>
  );

  const statsContent = (
    <div class="space-y-4">
      <div class="flex flex-wrap items-center justify-between gap-2 font-mono text-xs text-zinc-600 dark:text-zinc-400">
        <span>
          Auto-refresh every <strong class="text-zinc-900 dark:text-zinc-100">5s</strong> on this tab.
        </span>
        {lastRefresh && (
          <span class="tabular-nums">
            Last update: {lastRefresh.toLocaleTimeString()}
          </span>
        )}
      </div>
      <Card title="Summary">{statTable(statsRows)}</Card>
      <Card title="Latency by peer (bars)">
        <div class="grid max-h-[70vh] gap-3 overflow-y-auto md:grid-cols-2">
          {peers.map((p) => (
            <PeerLatencyPanel key={p.id} peer={p} />
          ))}
        </div>
      </Card>
    </div>
  );

  const actionsContent = (
    <div class="grid max-w-3xl gap-4">
      <Card title="Auth (reload after change)">
        <p class="mb-2 font-mono text-xs text-zinc-600 dark:text-zinc-400">
          If the daemon has username/password set, enter them here and reload
          the page.
        </p>
        <label class="mb-2 block font-mono text-xs">
          Username
          <input
            class="mt-1 w-full border-2 border-zinc-900 bg-white px-2 py-1 font-mono dark:border-zinc-100 dark:bg-zinc-950"
            value={authUser}
            onInput={(e) => setAuthUser((e.target as HTMLInputElement).value)}
          />
        </label>
        <label class="mb-2 block font-mono text-xs">
          Password
          <input
            type="password"
            class="mt-1 w-full border-2 border-zinc-900 bg-white px-2 py-1 font-mono dark:border-zinc-100 dark:bg-zinc-950"
            value={authPass}
            onInput={(e) => setAuthPass((e.target as HTMLInputElement).value)}
          />
        </label>
      </Card>
      <Card title="Connect (local_alsa_listener)">
        <p class="mb-2 font-mono text-xs text-zinc-600 dark:text-zinc-400">
          Same as CLI: optional name, hostname, port.
        </p>
        <input
          placeholder="name (optional)"
          class="mb-2 w-full border-2 border-zinc-900 px-2 py-1 font-mono dark:border-zinc-100"
          value={connectName}
          onInput={(e) => setConnectName((e.target as HTMLInputElement).value)}
        />
        <input
          placeholder="hostname"
          class="mb-2 w-full border-2 border-zinc-900 px-2 py-1 font-mono dark:border-zinc-100"
          value={connectHost}
          onInput={(e) => setConnectHost((e.target as HTMLInputElement).value)}
        />
        <input
          placeholder="port"
          class="mb-2 w-full border-2 border-zinc-900 px-2 py-1 font-mono dark:border-zinc-100"
          value={connectPort}
          onInput={(e) => setConnectPort((e.target as HTMLInputElement).value)}
        />
        <Button
          onClick={async () => {
            try {
              const params =
                connectName.trim() === ""
                  ? [connectHost, connectPort]
                  : [connectName, connectHost, connectPort];
              await rpc.call("connect", params);
              await refresh();
            } catch (e) {
              setStatus(String(e));
            }
          }}
        >
          connect
        </Button>
      </Card>
      <Card title="router.create (JSON)">
        <textarea
          class="mb-2 h-32 w-full border-2 border-zinc-900 bg-white p-2 font-mono text-xs dark:border-zinc-100 dark:bg-zinc-950"
          value={createJson}
          onInput={(e) =>
            setCreateJson((e.target as HTMLTextAreaElement).value)
          }
        />
        <Button
          onClick={async () => {
            try {
              const o = JSON.parse(createJson) as Record<string, unknown>;
              await rpc.call("router.create", o);
              await refresh();
            } catch (e) {
              setStatus(String(e));
            }
          }}
        >
          router.create
        </Button>
      </Card>
      <Card title="router.connect">
        <input
          placeholder="from peer id"
          class="mb-2 w-full border-2 border-zinc-900 px-2 py-1 font-mono dark:border-zinc-100"
          value={fromId}
          onInput={(e) => setFromId((e.target as HTMLInputElement).value)}
        />
        <input
          placeholder="to peer id"
          class="mb-2 w-full border-2 border-zinc-900 px-2 py-1 font-mono dark:border-zinc-100"
          value={toId}
          onInput={(e) => setToId((e.target as HTMLInputElement).value)}
        />
        <Button
          onClick={async () => {
            try {
              await rpc.call("router.connect", {
                from: Number(fromId),
                to: Number(toId),
              });
              await refresh();
            } catch (e) {
              setStatus(String(e));
            }
          }}
        >
          connect peers
        </Button>
      </Card>
    </div>
  );

  const moreContent = (
    <Card title="More">
      <p class="font-mono text-sm text-zinc-600 dark:text-zinc-400">
        Reserved for future tabs (logs, mDNS browser, …).
      </p>
    </Card>
  );

  const tabs: TabDef[] = [
    { id: "overview", label: "Overview", content: overviewContent },
    { id: "connections", label: "Connections", content: connectionsContent },
    { id: "stats", label: "Statistics", content: statsContent },
    { id: "actions", label: "Actions", content: actionsContent },
    { id: "more", label: "More", content: moreContent },
  ];

  return (
    <div class="mx-auto max-w-7xl p-4 font-sans">
      <header class="mb-6 flex flex-wrap items-end justify-between gap-4 border-b-4 border-zinc-900 pb-4 dark:border-zinc-100">
        <div>
          <h1 class="font-mono text-2xl font-black uppercase tracking-tight">
            rtpmidid
          </h1>
          <p class="font-mono text-xs text-zinc-600 dark:text-zinc-400">
            Web control · JSON-RPC over WebSocket
          </p>
          {data?.version && (
            <p class="mt-1 font-mono text-xs text-zinc-500">
              Daemon <span class="font-bold text-zinc-800 dark:text-zinc-200">{data.version}</span>
              {lastRefresh && (
                <>
                  {" "}
                  · refreshed{" "}
                  <span class="tabular-nums">{lastRefresh.toLocaleTimeString()}</span>
                </>
              )}
            </p>
          )}
        </div>
        <div class="flex flex-wrap items-center gap-2">
          <Button onClick={() => void refresh()}>Refresh now</Button>
          <Button
            onClick={() => {
              setDark(!dark);
            }}
          >
            {dark ? "Light" : "Dark"} mode
          </Button>
        </div>
      </header>
      <p class="mb-4 font-mono text-xs text-amber-800 dark:text-amber-300">
        {status}
      </p>
      <Tabs tabs={tabs} active={tab} onChange={setTab} />
    </div>
  );
}
