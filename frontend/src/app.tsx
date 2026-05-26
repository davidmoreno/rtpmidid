import { useCallback, useEffect, useMemo, useRef, useState } from "preact/hooks";
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
import { useUiTheme } from "./theme";
import type { StatusResult } from "./tabs/types";
import { AboutTab } from "./tabs/AboutTab";
import { ActionsTab } from "./tabs/ActionsTab";
import { ConnectionsTab } from "./tabs/ConnectionsTab";
import { MdnsTab } from "./tabs/MdnsTab";
import { DevicesTab } from "./tabs/DevicesTab";
import { PeersTab } from "./tabs/PeersTab";
import { SettingsTab } from "./tabs/SettingsTab";
import { MidiMonitorStandalone } from "./components/MidiMonitorStandalone";
import {
  parseConnectionsListResult,
  type PersistedConnectionRow,
} from "./persistedConnections";

function parseMonitorUuidFromHash(): string | null {
  if (typeof window === "undefined") return null;
  const raw = window.location.hash.startsWith("#")
    ? window.location.hash.slice(1)
    : window.location.hash;
  if (!raw.startsWith("monitor")) return null;
  const q = raw.indexOf("?");
  if (q === -1) return null;
  const params = new URLSearchParams(raw.slice(q + 1));
  const u = params.get("uuid");
  return u && u.trim().length > 0 ? u.trim() : null;
}

const AUTO_TABS = new Set([
  "devices",
  "connections",
  "peers",
  "mdns",
  "about",
]);

export function App() {
  const [monitorStandaloneUuid, setMonitorStandaloneUuid] = useState<
    string | null
  >(() => parseMonitorUuidFromHash());
  const [theme, setTheme] = useUiTheme();
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
    let h = raw.startsWith("#") ? raw.slice(1) : raw;
    if (h === "peer_cards") h = "devices";
    return h || "devices";
  });
  /* Kept null - the Connections page now opens the Devices tab instead of
     Peers, so there's no caller that highlights a router peer. The PeersTab
     prop is left in place because the table component still consumes it. */
  const [highlightPeerId] = useState<number | null>(null);
  const highlightClearTimer = useRef<number | undefined>(undefined);
  const [highlightConnectionRowId, setHighlightConnectionRowId] = useState<
    string | null
  >(null);
  const connectionHighlightClearTimer = useRef<number | undefined>(undefined);
  const [highlightEndpointId, setHighlightEndpointId] = useState<string | null>(
    null,
  );
  const endpointHighlightClearTimer = useRef<number | undefined>(undefined);
  const [lastRefresh, setLastRefresh] = useState<Date | null>(null);
  const [alsaSeq, setAlsaSeq] = useState<MidiAlsaSeqEntry[]>([]);
  const [rawmidi, setRawmidi] = useState<MidiRawmidiEntry[]>([]);
  const [alsaSubs, setAlsaSubs] = useState<unknown[]>([]);
  const [savedConnections, setSavedConnections] = useState<
    PersistedConnectionRow[]
  >([]);
  const [connectionsDbEnabled, setConnectionsDbEnabled] = useState(false);

  const rpc = useMemo(
    () =>
      new RpcClient(
        (m) => setStatus(m),
        (ev) => console.debug("event", ev),
      ),
    [],
  );

  const refreshInFlightRef = useRef(false);

  const loadConnectionsDb = useCallback(async () => {
    try {
      const raw = await rpc.call("connections.list", {});
      const parsed = parseConnectionsListResult(raw);
      setConnectionsDbEnabled(parsed.enabled);
      setSavedConnections(parsed.connections ?? []);
    } catch (e) {
      console.debug("connections.list failed", e);
    }
  }, [rpc]);

  const refresh = useCallback(async () => {
    if (refreshInFlightRef.current) return;
    refreshInFlightRef.current = true;
    try {
      const r = (await rpc.call("status", {})) as StatusResult;
      setData(r);
      setLastRefresh(new Date());
      if (tab === "devices" || tab === "connections") {
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
          console.debug("midi list refresh failed", e);
        }
      }
      await loadConnectionsDb();
    } catch (e) {
      setStatus(String(e));
    } finally {
      refreshInFlightRef.current = false;
    }
  }, [rpc, tab, loadConnectionsDb]);

  const refreshRef = useRef(refresh);
  refreshRef.current = refresh;

  /* Jump to Devices and pulse the matching card. The endpoint id format
     matches the endpoint cards in PeersCards (alsa:c:p / peer:N / mdns:Name::Port). */
  const onOpenEndpointInDevices = useCallback((endpointId: string) => {
    setTab("devices");
    setHighlightEndpointId(endpointId);
    if (endpointHighlightClearTimer.current !== undefined) {
      window.clearTimeout(endpointHighlightClearTimer.current);
    }
    endpointHighlightClearTimer.current = window.setTimeout(() => {
      setHighlightEndpointId(null);
      endpointHighlightClearTimer.current = undefined;
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
      if (endpointHighlightClearTimer.current !== undefined) {
        window.clearTimeout(endpointHighlightClearTimer.current);
      }
    },
    [],
  );

  useEffect(() => {
    rpc.setOnReconnect(() => {
      void refreshRef.current();
    });
    rpc
      .connect()
      .then(() => refreshRef.current())
      .catch((e) => setStatus(String(e)));
    return () => {
      rpc.setOnReconnect(null);
      rpc.disconnect();
    };
  }, [rpc]);

  useEffect(() => {
    if (tab !== "devices" && tab !== "connections") return;
    void refresh();
  }, [tab, refresh]);

  /** Load saved pairs when opening Connections (refresh may have been skipped while in-flight). */
  useEffect(() => {
    if (tab !== "connections" || lastRefresh === null) return;
    void loadConnectionsDb();
  }, [tab, lastRefresh, loadConnectionsDb]);

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

  const tabs: TabDef[] = [
    {
      id: "devices",
      label: "Devices",
      content: (
        <DevicesTab
          refreshIntervalMs={refreshIntervalMs}
          lastRefresh={lastRefresh}
          peers={peers}
          mdnsRemotes={mdnsParsed.remotes}
          alsaSeq={alsaSeq}
          rawmidi={rawmidi}
          alsaSubs={alsaSubs}
          highlightEndpointId={highlightEndpointId}
          rpc={rpc}
          onAfterAction={refresh}
          onStatus={setStatus}
        />
      ),
    },
    {
      id: "connections",
      label: "Connections",
      content: (
        <ConnectionsTab
          refreshIntervalMs={refreshIntervalMs}
          lastRefresh={lastRefresh}
          liveConnections={connections}
          savedConnections={savedConnections}
          alsaSubs={alsaSubs}
          dbEnabled={connectionsDbEnabled}
          highlightConnectionRowId={highlightConnectionRowId}
          onOpenInDevices={onOpenEndpointInDevices}
          rpc={rpc}
          peers={peers}
          mdnsRemotes={mdnsParsed.remotes}
          alsaSeq={alsaSeq}
          rawmidi={rawmidi}
          onAfterAction={refresh}
          onStatus={setStatus}
        />
      ),
    },
    {
      id: "peers",
      label: "Peers",
      content: (
        <PeersTab
          refreshIntervalMs={refreshIntervalMs}
          lastRefresh={lastRefresh}
          peers={peers}
          edges={edges}
          highlightPeerId={highlightPeerId}
        />
      ),
    },
    {
      id: "mdns",
      label: "mDNS",
      content: (
        <MdnsTab
          refreshIntervalMs={refreshIntervalMs}
          lastRefresh={lastRefresh}
          status={mdnsParsed.status}
          announcements={mdnsParsed.announcements}
          remotes={mdnsParsed.remotes}
          rpc={rpc}
          onWireMdnsToLocal={wireMdnsRemoteToLocal}
        />
      ),
    },
    {
      id: "about",
      label: "About",
      content: (
        <AboutTab
          refreshIntervalMs={refreshIntervalMs}
          lastRefresh={lastRefresh}
          statsRows={statsRows}
        />
      ),
    },
    {
      id: "actions",
      label: "Actions",
      content: (
        <ActionsTab rpc={rpc} onRefresh={refresh} onStatus={setStatus} />
      ),
    },
    {
      id: "settings",
      label: "Settings",
      content: (
        <SettingsTab
          theme={theme}
          setTheme={setTheme}
          refreshIntervalMs={refreshIntervalMs}
          setRefreshIntervalMs={setRefreshIntervalMs}
        />
      ),
    },
  ];

  useEffect(() => {
    if (monitorStandaloneUuid) return;
    const ids = new Set(tabs.map((t) => t.id));
    if (!ids.has(tab)) {
      setTab("devices");
      return;
    }
    const next = `#${tab}`;
    if (window.location.hash !== next) {
      window.location.hash = next;
    }
  }, [tab, tabs, monitorStandaloneUuid]);

  useEffect(() => {
    const ids = new Set(tabs.map((t) => t.id));
    const onHash = () => {
      const mu = parseMonitorUuidFromHash();
      setMonitorStandaloneUuid(mu);
      if (mu) return;
      const raw = window.location.hash;
      let h = raw.startsWith("#") ? raw.slice(1) : raw;
      if (h === "peer_cards") h = "devices";
      if (h && ids.has(h)) setTab(h);
    };
    window.addEventListener("hashchange", onHash);
    onHash();
    return () => window.removeEventListener("hashchange", onHash);
  }, [tabs]);

  if (monitorStandaloneUuid) {
    return (
      <MidiMonitorStandalone
        uuid={monitorStandaloneUuid}
        rpc={rpc}
        onStatus={setStatus}
        onExit={() => {
          setMonitorStandaloneUuid(null);
          window.location.hash = "#devices";
        }}
      />
    );
  }

  return (
    <div class="ui-page">
      <header class="ui-header-bar mb-6">
        <div class="ui-page-inner flex flex-wrap items-end justify-between gap-4 pb-4 pt-4">
          <div>
            <h1 class="font-mono text-2xl font-black uppercase tracking-tight ui-text">
              rtpmidid
            </h1>
            <p class="font-mono text-xs ui-text-muted">
              Web control · JSON-RPC over WebSocket
            </p>
            {data?.version && (
              <p class="mt-1 font-mono text-xs ui-text-subtle">
                Daemon <span class="font-bold ui-text">{data.version}</span>
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
        </div>
      </header>
      <div class="ui-page-inner p-4 pt-0">
        <p class="mb-4 font-mono text-xs ui-banner-warn">{status}</p>
        <Tabs tabs={tabs} active={tab} onChange={setTab} />
      </div>
    </div>
  );
}
