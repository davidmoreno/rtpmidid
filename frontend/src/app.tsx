import { useCallback, useEffect, useMemo, useRef, useState } from "preact/hooks";
import { Button } from "./components/Button";
import { Card } from "./components/Card";
import { ConnectionsTable } from "./components/ConnectionsTable";
import { EdgesTable } from "./components/EdgesTable";
import { MdnsTables } from "./components/MdnsTables";
import { PeersCards } from "./components/PeersCards";
import { PeersTable } from "./components/PeersTable";
import { Tabs, type TabDef } from "./components/Tabs";
import {
  buildConnections,
  buildEdges,
  connectionRowIdLinkingPeers,
  normalizePeers,
  parseMdns,
} from "./model";
import { RpcClient } from "./rpc";
import {
  DEFAULT_STATUS_REFRESH_MS,
  parseStoredStatusRefreshMs,
  STATUS_REFRESH_CHOICES,
  STORAGE_KEY_STATUS_REFRESH_MS,
} from "./statusRefresh";
import {
  normalizeRtpMidiUdpPort,
  sanitizePeerBaseName,
  type WireLocalChoice,
} from "./midiEnumerate";
import {
  parseMidiAlsaSeqResult,
  parseMidiRawmidiResult,
  type MidiAlsaSeqEntry,
  type MidiRawmidiEntry,
} from "./midiEnumerate";

type StatusResult = {
  version?: string;
  router?: unknown[];
  mdns?: Record<string, unknown>;
  settings?: Record<string, unknown>;
};

const AUTO_TABS = new Set(["connections", "peer_cards", "peers", "mdns", "about"]);

/** Legacy `router.create` `{ type, ... }` maps to split RPC methods. */
const ROUTER_CREATE_TYPE_TO_METHOD: Record<string, string> = {
  local_rawmidi_t: "router.create.local_rawmidi",
  network_rtpmidi_client_t: "router.create.network_rtpmidi_client",
  network_rtpmidi_listener_t: "router.create.network_rtpmidi_listener",
  local_alsa_peer_t: "router.create.local_alsa_peer",
};

async function rpcCallRouterCreatePayload(
  rpc: RpcClient,
  payload: Record<string, unknown>,
): Promise<void> {
  const t = payload.type;
  if (typeof t !== "string") {
    throw new Error('router create JSON must include a string "type" field');
  }
  const method = ROUTER_CREATE_TYPE_TO_METHOD[t];
  if (!method) {
    throw new Error(`Unknown router.create type: ${t}`);
  }
  const { type: _drop, ...rest } = payload;
  await rpc.call(method, rest);
}

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
  const [refreshIntervalMs, setRefreshIntervalMs] = useState(() =>
    typeof localStorage !== "undefined"
      ? parseStoredStatusRefreshMs(
          localStorage.getItem(STORAGE_KEY_STATUS_REFRESH_MS),
        )
      : DEFAULT_STATUS_REFRESH_MS,
  );
  const [status, setStatus] = useState<string>("");
  const [data, setData] = useState<StatusResult | null>(null);
  const [tab, setTab] = useState(() => {
    const raw = typeof window !== "undefined" ? window.location.hash : "";
    const h = raw.startsWith("#") ? raw.slice(1) : raw;
    return h || "connections";
  });
  const [highlightPeerId, setHighlightPeerId] = useState<number | null>(null);
  const highlightClearTimer = useRef<number | undefined>(undefined);
  const [highlightConnectionRowId, setHighlightConnectionRowId] = useState<
    string | null
  >(null);
  const connectionHighlightClearTimer = useRef<number | undefined>(undefined);
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
  const [alsaSeq, setAlsaSeq] = useState<MidiAlsaSeqEntry[]>([]);
  const [rawmidi, setRawmidi] = useState<MidiRawmidiEntry[]>([]);
  const [alsaSubs, setAlsaSubs] = useState<unknown[]>([]);

  const rpc = useMemo(
    () =>
      new RpcClient(
        (m) => setStatus(m),
        (ev) => console.debug("event", ev),
      ),
    [],
  );

  /** Avoid overlapping `status` RPCs (periodic poll vs Actions-tab follow-up refresh). */
  const refreshInFlightRef = useRef(false);

  const refresh = useCallback(async () => {
    if (refreshInFlightRef.current) return;
    refreshInFlightRef.current = true;
    try {
      const r = (await rpc.call("status", {})) as StatusResult;
      setData(r);
      setLastRefresh(new Date());
      if (tab === "peer_cards") {
        try {
          const [rAlsa, rRaw, rSubs] = await Promise.all([
            rpc.call("midi.listAlsaSeq", {}),
            rpc.call("midi.listRawMidi", {}),
            rpc.call("midi.listAlsaSubscriptions", {}),
          ]);
          setAlsaSeq(parseMidiAlsaSeqResult(rAlsa) ?? []);
          setRawmidi(parseMidiRawmidiResult(rRaw) ?? []);
          setAlsaSubs(Array.isArray(rSubs) ? (rSubs as unknown[]) : []);
        } catch (e) {
          // Keep old lists on error; status banner already shows errors for status RPC.
          console.debug("midi list refresh failed", e);
        }
      }
    } catch (e) {
      setStatus(String(e));
    } finally {
      refreshInFlightRef.current = false;
    }
  }, [rpc, tab]);

  const onSelectPeerFromConnections = useCallback((id: number) => {
    setTab("peers");
    setHighlightPeerId(id);
    if (highlightClearTimer.current !== undefined) {
      window.clearTimeout(highlightClearTimer.current);
    }
    highlightClearTimer.current = window.setTimeout(() => {
      setHighlightPeerId(null);
      highlightClearTimer.current = undefined;
    }, 3000);
  }, []);

  useEffect(
    () => () => {
      if (highlightClearTimer.current !== undefined) {
        window.clearTimeout(highlightClearTimer.current);
      }
      if (connectionHighlightClearTimer.current !== undefined) {
        window.clearTimeout(connectionHighlightClearTimer.current);
      }
    },
    [],
  );

  useEffect(() => {
    rpc.setAuth(authUser, authPass);
    rpc
      .connect()
      .then(() => refresh())
      .catch((e) => setStatus(String(e)));
    return () => rpc.disconnect();
  }, []);

  // On entering the Peers (endpoint) tab, do an immediate refresh so ALSA/raw
  // enumeration is populated without waiting for the next poll tick.
  useEffect(() => {
    if (tab !== "peer_cards") return;
    void refresh();
  }, [tab, refresh]);

  /** Poll only after the previous `status` finishes; spacing is `refreshIntervalMs` between completions. */
  useEffect(() => {
    if (!AUTO_TABS.has(tab) || refreshIntervalMs <= 0) return undefined;
    let cancelled = false;
    let timerId: number | undefined;

    const step = async () => {
      await refresh();
      if (!cancelled) {
        timerId = window.setTimeout(step, refreshIntervalMs);
      }
    };

    timerId = window.setTimeout(step, refreshIntervalMs);

    return () => {
      cancelled = true;
      if (timerId !== undefined) window.clearTimeout(timerId);
    };
  }, [tab, refresh, refreshIntervalMs]);

  const routerRaw = data?.router ?? [];
  const peers = useMemo(() => normalizePeers(routerRaw), [routerRaw]);
  const edges = useMemo(() => buildEdges(peers), [peers]);
  const connections = useMemo(() => buildConnections(peers), [peers]);
  const mdnsParsed = useMemo(
    () => parseMdns(data?.mdns as Record<string, unknown> | undefined),
    [data?.mdns],
  );

  const wireMdnsRemoteToLocal = useCallback(
    async (args: {
      serviceName: string;
      target: string;
      port: number | string;
      local: WireLocalChoice;
    }) => {
      try {
        const snap = (await rpc.call("status", {})) as StatusResult;
        const idsBefore = new Set(
          normalizePeers(snap.router ?? []).map((p) => p.id),
        );

        const peerBase = sanitizePeerBaseName(
          args.local.listingLabel,
          args.serviceName,
        );
        const uniquePeerName = `WEB:${peerBase}:${Date.now().toString(36)}`.slice(
          0,
          64,
        );

        if (args.local.mode === "alsa_seq") {
          await rpc.call("router.create.local_alsa_peer", {
            name: uniquePeerName,
            alsa_client: args.local.client,
            alsa_port: args.local.port,
          });
        } else {
          await rpc.call("router.create.local_rawmidi", {
            name: uniquePeerName,
            device: args.local.device,
          });
        }

        const afterLocal = (await rpc.call("status", {})) as StatusResult;
        const peersAfterLocal = normalizePeers(afterLocal.router ?? []);
        const newLocals = peersAfterLocal.filter(
          (p) =>
            !idsBefore.has(p.id) &&
            (p.type === "local_alsa_peer_t" ||
              p.type === "local_rawmidi_peer_t"),
        );
        const localPeer = newLocals.sort((a, b) => b.id - a.id)[0];
        if (!localPeer) {
          throw new Error(
            "Could not create local ALSA sequencer or raw MIDI peer",
          );
        }

        const idsMid = new Set(peersAfterLocal.map((p) => p.id));

        const safe =
          args.serviceName.replace(/\s+/g, " ").trim().slice(0, 48) ||
          "Remote";
        const clientName = `WEB · ${safe}`;

        await rpc.call("router.create.network_rtpmidi_client", {
          name: clientName,
          hostname: args.target.trim(),
          port: normalizeRtpMidiUdpPort(args.port),
        });

        const afterClient = (await rpc.call("status", {})) as StatusResult;
        const peersAfterClient = normalizePeers(afterClient.router ?? []);
        const newClients = peersAfterClient.filter(
          (p) =>
            !idsMid.has(p.id) && p.type === "network_rtpmidi_client_t",
        );
        const clientPeer = newClients.sort((a, b) => b.id - a.id)[0];
        if (!clientPeer) {
          throw new Error("Could not find new RTP MIDI client peer after create");
        }

        await rpc.call("router.connect", {
          from: localPeer.id,
          to: clientPeer.id,
        });
        await rpc.call("router.connect", {
          from: clientPeer.id,
          to: localPeer.id,
        });

        const fin = (await rpc.call("status", {})) as StatusResult;
        setData(fin);
        setLastRefresh(new Date());
        setStatus("");

        const peersFinal = normalizePeers(fin.router ?? []);
        const rowId = connectionRowIdLinkingPeers(
          peersFinal,
          localPeer.id,
          clientPeer.id,
        );
        setHighlightConnectionRowId(rowId);
        setTab("connections");

        if (connectionHighlightClearTimer.current !== undefined) {
          window.clearTimeout(connectionHighlightClearTimer.current);
        }
        connectionHighlightClearTimer.current = window.setTimeout(() => {
          setHighlightConnectionRowId(null);
          connectionHighlightClearTimer.current = undefined;
        }, 4000);
      } catch (e) {
        setStatus(String(e));
      }
    },
    [rpc],
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

  const refreshBanner = (
    <div class="flex flex-wrap items-center justify-between gap-2 font-mono text-xs text-zinc-600 dark:text-zinc-400">
      <span>
        {refreshIntervalMs <= 0 ? (
          <>
            Automatic polling{" "}
            <strong class="text-zinc-900 dark:text-zinc-100">off</strong> — data
            updates only after Actions commands or reloading.
          </>
        ) : (
          <>
            Poll every{" "}
            <strong class="text-zinc-900 dark:text-zinc-100">
              {STATUS_REFRESH_CHOICES.find((c) => c.ms === refreshIntervalMs)
                ?.label ?? `${refreshIntervalMs / 1000}s`}
            </strong>{" "}
            after each status response on this tab.
          </>
        )}
      </span>
      {lastRefresh && (
        <span class="tabular-nums">
          Last update:{" "}
          {lastRefresh.toLocaleTimeString([], {
            hour: "2-digit",
            minute: "2-digit",
            second: "2-digit",
            hour12: false,
          })}
        </span>
      )}
    </div>
  );

  const peersContent = (
    <div class="space-y-4">
      {refreshBanner}
      <Card title="Peers">
        <PeersTable peers={peers} highlightPeerId={highlightPeerId} />
      </Card>
      <Card title="Router edges">
        <EdgesTable edges={edges} />
      </Card>
    </div>
  );

  const peerCardsContent = (
    <div class="space-y-4">
      {refreshBanner}
      <PeersCards
        peers={peers}
        mdnsRemotes={mdnsParsed.remotes}
        alsaSeq={alsaSeq}
        rawmidi={rawmidi}
        alsaSubs={alsaSubs}
        rpc={rpc}
        onAfterAction={refresh}
        onStatus={setStatus}
      />
    </div>
  );

  const connectionsContent = (
    <div class="space-y-4">
      {refreshBanner}
      <Card title="Connections (router + RTP)">
        <ConnectionsTable
          rows={connections}
          highlightConnectionRowId={highlightConnectionRowId}
          onSelectPeer={onSelectPeerFromConnections}
        />
      </Card>
    </div>
  );

  const mdnsContent = (
    <div class="space-y-4">
      {refreshBanner}
      <Card title="mDNS">
        <MdnsTables
          status={mdnsParsed.status}
          announcements={mdnsParsed.announcements}
          remotes={mdnsParsed.remotes}
          rpc={rpc}
          onWireMdnsToLocal={wireMdnsRemoteToLocal}
        />
      </Card>
    </div>
  );

  const aboutContent = (
    <div class="space-y-4">
      {refreshBanner}
      <Card title="About this UI">
        <p class="mb-3 font-mono text-xs leading-relaxed text-zinc-700 dark:text-zinc-300">
          Web dashboard for{" "}
          <strong class="text-zinc-900 dark:text-zinc-100">rtpmidid</strong>: live
          connections, router peers, RTP view, and mDNS discovery. Commands are
          sent over JSON-RPC on a WebSocket to the daemon.
        </p>
        {statTable(statsRows)}
        <p class="mt-3 font-mono text-xs text-zinc-600 dark:text-zinc-400">
          Latency bars use a shared piecewise scale and color tiers in{" "}
          <code class="rounded bg-zinc-200 px-1 dark:bg-zinc-800">latencyScale.ts</code>
          . Each row shows one bar for the <strong>sum</strong> of available
          samples; hover the bar for a per-metric breakdown.
        </p>
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
              const params: Record<string, string> = {
                hostname: connectHost.trim(),
              };
              if (connectPort.trim() !== "") {
                params.port = connectPort.trim();
              }
              if (connectName.trim() !== "") {
                params.name = connectName.trim();
              }
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
      <Card title="router.create (typed JSON)">
        <p class="mb-2 font-mono text-xs text-zinc-600 dark:text-zinc-400">
          Use legacy shape{" "}
          <code class="rounded bg-zinc-200 px-1 dark:bg-zinc-800">
            {`{"type":"local_rawmidi_t",...}`}
          </code>{" "}
          (type keys from{" "}
          <code class="rounded bg-zinc-200 px-1 dark:bg-zinc-800">
            router.create.list
          </code>
          ), or full JSON-RPC{" "}
          <code class="rounded bg-zinc-200 px-1 dark:bg-zinc-800">
            {`{"method":"…","params":{…}}`}
          </code>
          .
        </p>
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
              if (typeof o.method === "string") {
                await rpc.call(o.method, (o.params as object) ?? {});
              } else {
                await rpcCallRouterCreatePayload(rpc, o);
              }
              await refresh();
            } catch (e) {
              setStatus(String(e));
            }
          }}
        >
          send
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

  const tabs: TabDef[] = [
    { id: "connections", label: "Connections", content: connectionsContent },
    { id: "peer_cards", label: "Peers", content: peerCardsContent },
    { id: "peers", label: "Peers", content: peersContent },
    { id: "mdns", label: "mDNS", content: mdnsContent },
    { id: "about", label: "About", content: aboutContent },
    { id: "actions", label: "Actions", content: actionsContent },
  ];

  // Keep active tab in location hash for reload/back-forward.
  useEffect(() => {
    const ids = new Set(tabs.map((t) => t.id));
    if (!ids.has(tab)) {
      setTab("connections");
      return;
    }
    const next = `#${tab}`;
    if (window.location.hash !== next) {
      window.location.hash = next;
    }
  }, [tab, tabs]);

  useEffect(() => {
    const ids = new Set(tabs.map((t) => t.id));
    const onHash = () => {
      const raw = window.location.hash;
      const h = raw.startsWith("#") ? raw.slice(1) : raw;
      if (h && ids.has(h)) setTab(h);
    };
    window.addEventListener("hashchange", onHash);
    // If we loaded an invalid hash before tabs existed, fix it now.
    onHash();
    return () => window.removeEventListener("hashchange", onHash);
  }, [tabs]);

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
                  <span class="tabular-nums">
                    {lastRefresh.toLocaleTimeString([], {
                      hour: "2-digit",
                      minute: "2-digit",
                      second: "2-digit",
                      hour12: false,
                    })}
                  </span>
                </>
              )}
            </p>
          )}
        </div>
        <div class="flex flex-wrap items-center gap-2">
          <label class="flex items-center gap-2 font-mono text-xs">
            <span class="whitespace-nowrap text-zinc-600 dark:text-zinc-400">
              Poll
            </span>
            <select
              class="border-2 border-zinc-900 bg-white px-2 py-1.5 font-mono dark:border-zinc-100 dark:bg-zinc-950"
              value={refreshIntervalMs}
              onChange={(e) => {
                const ms = Number((e.target as HTMLSelectElement).value);
                setRefreshIntervalMs(ms);
                localStorage.setItem(STORAGE_KEY_STATUS_REFRESH_MS, String(ms));
              }}
            >
              {STATUS_REFRESH_CHOICES.map((c) => (
                <option key={c.ms} value={c.ms}>
                  {c.label}
                </option>
              ))}
            </select>
          </label>
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
