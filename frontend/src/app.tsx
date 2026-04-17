import { useCallback, useEffect, useMemo, useState } from "preact/hooks";
import { Button } from "./components/Button";
import { Card } from "./components/Card";
import { Tabs, type TabDef } from "./components/Tabs";
import { RpcClient } from "./rpc";

type StatusResult = {
  version?: string;
  router?: unknown[];
  mdns?: Record<string, unknown>;
  settings?: Record<string, unknown>;
};

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

  const router = (data?.router ?? []) as Record<string, unknown>[];

  const statsRows = useMemo(() => {
    const edges = router.reduce(
      (n, p) => n + ((p.send_to as number[])?.length ?? 0),
      0,
    );
    let rtpPeers = 0;
    for (const p of router) {
      const peers = p.peers as unknown[] | undefined;
      if (peers) rtpPeers += peers.length;
      const pr = p.peer as Record<string, unknown> | undefined;
      if (pr) rtpPeers += 1;
    }
    return [
      { k: "Router peers", v: String(router.length) },
      { k: "Router edges (send_to)", v: String(edges) },
      { k: "RTP peer rows (approx)", v: String(rtpPeers) },
      { k: "Version", v: String(data?.version ?? "—") },
    ];
  }, [data, router]);

  const overviewContent = (
    <div class="grid gap-4 lg:grid-cols-2">
      <Card title="Router">
        <pre class="max-h-[28rem] overflow-auto whitespace-pre-wrap break-all font-mono text-xs">
          {JSON.stringify(router, null, 2)}
        </pre>
      </Card>
      <Card title="mDNS">
        <pre class="max-h-[28rem] overflow-auto whitespace-pre-wrap font-mono text-xs">
          {JSON.stringify(data?.mdns ?? {}, null, 2)}
        </pre>
      </Card>
    </div>
  );

  const statsContent = (
    <div class="grid gap-4 lg:grid-cols-2">
      <Card title="Counts">{statTable(statsRows)}</Card>
      <Card title="Latency (per peer)">
        <div class="max-h-[28rem] overflow-auto font-mono text-xs">
          {router.map((p) => (
            <div
              key={String(p.id)}
              class="mb-3 border-2 border-zinc-800 p-2 dark:border-zinc-200"
            >
              <div class="font-bold">
                #{String(p.id)} {String(p.name ?? "")} ({String(p.type ?? "")})
              </div>
              <div class="mt-1 text-zinc-600 dark:text-zinc-400">
                internal_latency_ms:{" "}
                {JSON.stringify(p.internal_latency_ms ?? null)}
              </div>
              <div class="text-zinc-600 dark:text-zinc-400">
                peer / peers RTP: {JSON.stringify(p.peer ?? p.peers ?? null)}
              </div>
            </div>
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
    { id: "stats", label: "Statistics", content: statsContent },
    { id: "actions", label: "Actions", content: actionsContent },
    { id: "more", label: "More", content: moreContent },
  ];

  return (
    <div class="mx-auto max-w-6xl p-4 font-sans">
      <header class="mb-6 flex flex-wrap items-end justify-between gap-4 border-b-4 border-zinc-900 pb-4 dark:border-zinc-100">
        <div>
          <h1 class="font-mono text-2xl font-black uppercase tracking-tight">
            rtpmidid
          </h1>
          <p class="font-mono text-xs text-zinc-600 dark:text-zinc-400">
            Web control · JSON-RPC over WebSocket
          </p>
        </div>
        <div class="flex flex-wrap items-center gap-2">
          <Button onClick={() => void refresh()}>Refresh status</Button>
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
