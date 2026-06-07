import { useCallback, useEffect, useMemo, useRef, useState } from "preact/hooks";
import { Tabs, type TabDef } from "./components/Tabs";
import {
  buildConnections,
  buildEdges,
  connectionRowIdLinkingPeers,
  normalizePeers,
  parseMdns,
} from "./model";
import { RpcClient, type RpcConnectionState, type JsonRpcResponse } from "./rpc";
import { ConnectionBanner } from "./components/ConnectionBanner";
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
import {
  identityFromForm,
  identityFromRtpClientConnect,
  serializeIdentity,
} from "./deviceIdentity";
import { useUiTheme } from "./theme";
import type { StatusResult } from "./tabs/types";
import { daemonStore, useDaemonState } from "./store";
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
import {
  parseDevicesListResult,
  type RegistryDevice,
} from "./devicesList";

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

/** Event channels the frontend subscribes to after connecting. */
const SUBSCRIBE_CHANNELS = [
  "router.peer_added",
  "router.peer_removed",
  "router.edge_added",
  "router.edge_removed",
  "router.peer_updated",
  "mdns.discovered",
  "mdns.removed",
  "mdns.announcement_changed",
];

/**
 * Route incoming WebSocket events to the daemon store.
 */
function handleWsEvent(ev: JsonRpcResponse) {
  if (!ev.event || ev.params === undefined) return;

  const params = ev.params as Record<string, unknown>;

  switch (ev.event) {
    case "router.peer_added":
      daemonStore.reduce({
        type: "peer_added",
        peer: params as Record<string, unknown>,
      });
      break;

    case "router.peer_removed":
      daemonStore.reduce({
        type: "peer_removed",
        peer_id: Number((params as { peer_id: unknown }).peer_id),
      });
      break;

    case "router.edge_added": {
      const p = params as { from: unknown; to: unknown };
      daemonStore.reduce({
        type: "edge_added",
        from: Number(p.from),
        to: Number(p.to),
      });
      break;
    }

    case "router.edge_removed": {
      const p = params as { from: unknown; to: unknown };
      daemonStore.reduce({
        type: "edge_removed",
        from: Number(p.from),
        to: Number(p.to),
      });
      break;
    }

    case "mdns.discovered": {
      const p = params as {
        name: string;
        hostname: string;
        ip: string;
        port: number;
      };
      daemonStore.reduce({
        type: "mdns_discovered",
        remote: {
          name: p.name,
          hostname: p.hostname,
          ip: p.ip ?? p.hostname,
          port: p.port,
        },
      });
      break;
    }

    case "mdns.removed": {
      const p = params as { name: string; address: string; port: number };
      daemonStore.reduce({
        type: "mdns_removed",
        name: p.name,
        address: p.address,
        port: p.port,
      });
      break;
    }

    case "router.peer_updated":
      daemonStore.reduce({
        type: "peer_added",
        peer: params as Record<string, unknown>,
      });
      break;

    case "mdns.announcement_changed": {
      // Full mDNS snapshot — reload into store
      const p = params as {
        status: string;
        announcements?: unknown[];
        remote_announcements?: unknown[];
      };
      daemonStore.loadSnapshot({
        mdns: {
          status: p.status,
          announcements: p.announcements ?? [],
          remote_announcements: p.remote_announcements ?? [],
        },
      });
      break;
    }
  }
}

export function App() {
  const [monitorStandaloneUuid, setMonitorStandaloneUuid] = useState<
    string | null
  >(() => parseMonitorUuidFromHash());
  const [theme, setTheme] = useUiTheme();
  const [status, setStatus] = useState<string>("");
  const [connState, setConnState] = useState<RpcConnectionState | null>(null);
  const [tab, setTab] = useState(() => {
    const raw = typeof window !== "undefined" ? window.location.hash : "";
    let h = raw.startsWith("#") ? raw.slice(1) : raw;
    if (h === "peer_cards") h = "devices";
    return h || "devices";
  });
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
  const [registryDevices, setRegistryDevices] = useState<RegistryDevice[]>([]);
  const [registryEnabled, setRegistryEnabled] = useState(false);

  // ── Event-driven daemon state ──────────────────────────────────────
  const daemonState = useDaemonState();

  const rpc = useMemo(
    () =>
      new RpcClient(
        (m) => setStatus(m),
        handleWsEvent,
      ),
    [],
  );

  const loadConnectionsDb = useCallback(async () => {
    try {
      const raw = await rpc.call("connections.list", {});
      const parsed = parseConnectionsListResult(raw);
      setConnectionsDbEnabled(parsed.enabled ?? false);
      setSavedConnections(parsed.connections ?? []);
    } catch (e) {
      console.debug("connections.list failed", e);
    }
  }, [rpc]);

  const loadDevicesRegistry = useCallback(async () => {
    try {
      const raw = await rpc.call("devices.list", {});
      const parsed = parseDevicesListResult(raw);
      setRegistryEnabled(parsed.enabled);
      setRegistryDevices(parsed.devices);
    } catch (e) {
      console.debug("devices.list failed", e);
    }
  }, [rpc]);

  /** Fetch ALSA MIDI lists on demand (tab switch or initial load). */
  const loadMidiLists = useCallback(async () => {
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
      console.debug("midi list fetch failed", e);
    }
  }, [rpc]);

  /** Called after each (re)connect: subscribe + load initial snapshot. */
  const onSessionReady = useCallback(async () => {
    try {
      // Subscribe first so we don't miss events between status and subscription
      await rpc.subscribe(SUBSCRIBE_CHANNELS);

      // Load initial snapshot into the store
      const statusResult = (await rpc.call("status", {})) as StatusResult;
      daemonStore.loadSnapshot(statusResult);
      setLastRefresh(new Date());

      // Load auxiliary data
      await loadMidiLists();
      await loadConnectionsDb();
      await loadDevicesRegistry();
    } catch (e) {
      setStatus(String(e));
    }
  }, [rpc, loadMidiLists, loadConnectionsDb, loadDevicesRegistry]);

  const reconnectHandlerRef = useRef(onSessionReady);
  reconnectHandlerRef.current = onSessionReady;

  /* Jump to Devices and pulse the matching card (device identity string). */
  const onOpenEndpointInDevices = useCallback((identity: string) => {
    setTab("devices");
    setHighlightEndpointId(identity);
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
    rpc.setOnConnectionStateChange(setConnState);
    rpc.setOnReconnect(() => {
      void reconnectHandlerRef.current();
    });

    const onPageShow = () => {
      if (!rpc.isConnected()) {
        rpc.forceReconnect();
      }
    };
    window.addEventListener("pageshow", onPageShow);

    rpc
      .connect()
      .then(() => reconnectHandlerRef.current())
      .catch((e) => setStatus(String(e)));

    return () => {
      window.removeEventListener("pageshow", onPageShow);
      rpc.setOnConnectionStateChange(null);
      rpc.setOnReconnect(null);
      rpc.disconnect();
    };
  }, [rpc]);

  // Fetch auxiliary data on tab switch to Devices/Connections
  useEffect(() => {
    if (tab !== "devices" && tab !== "connections") return;
    void loadMidiLists();
  }, [tab, loadMidiLists]);

  // Fetch DB data on tab switch to Connections
  useEffect(() => {
    if (tab !== "connections") return;
    void loadConnectionsDb();
  }, [tab, loadConnectionsDb]);

  // Fetch registry on tab switch to Devices
  useEffect(() => {
    if (tab !== "devices") return;
    void loadDevicesRegistry();
  }, [tab, loadDevicesRegistry]);

  const peers = useMemo(
    () => daemonState.peers,
    [daemonState.peers],
  );
  const edges = useMemo(() => buildEdges(peers), [peers]);
  const connections = useMemo(() => buildConnections(peers), [peers]);
  const mdnsParsed = useMemo(() => daemonState.mdns, [daemonState.mdns]);

  const wireMdnsRemoteToLocal = useCallback(
    async (args: {
      serviceName: string;
      target: string;
      port: number | string;
      local: WireLocalChoice;
    }) => {
      try {
        // Snapshot before creating peers to track new IDs
        const snapBefore = (await rpc.call("status", {})) as StatusResult;
        const idsBefore = new Set(
          normalizePeers(snapBefore.router ?? []).map((p) => p.id),
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
          const parsed = identityFromForm("alsa_seq", {
            client: String(args.local.client),
            port: String(args.local.port),
            name: uniquePeerName,
          });
          if (!parsed) throw new Error("Invalid ALSA identity");
          await rpc.call("router.create", {
            identity: serializeIdentity(parsed),
          });
        } else {
          const parsed = identityFromForm("rawmidi", {
            device: String(args.local.device),
            name: uniquePeerName,
          });
          if (!parsed) throw new Error("Invalid raw MIDI identity");
          await rpc.call("router.create", {
            identity: serializeIdentity(parsed),
          });
        }

        const safe =
          args.serviceName.replace(/\s+/g, " ").trim().slice(0, 48) ||
          "Remote";
        const clientName = `WEB · ${safe}`;

        const clientIdentity = identityFromRtpClientConnect(
          args.target.trim(),
          normalizeRtpMidiUdpPort(args.port),
          clientName,
        );
        if (!clientIdentity) throw new Error("Invalid RTP client identity");
        await rpc.call("router.create", { identity: clientIdentity });

        // Get final snapshot to find new peers
        const fin = (await rpc.call("status", {})) as StatusResult;
        const peersFinal = normalizePeers(fin.router ?? []);
        const newPeers = peersFinal.filter((p) => !idsBefore.has(p.id));

        const localPeer = newPeers.find(
          (p) =>
            p.type === "peer_device_alsa_seq_t" ||
            p.type === "peer_device_rawmidi_t",
        );
        const clientPeer = newPeers.find(
          (p) => p.type === "peer_device_rtpmidi_client_t",
        );

        if (localPeer && clientPeer) {
          await rpc.call("router.connect", {
            from: localPeer.id,
            to: clientPeer.id,
          });
          await rpc.call("router.connect", {
            from: clientPeer.id,
            to: localPeer.id,
          });

          daemonStore.loadSnapshot(fin);
          setLastRefresh(new Date());
          setStatus("");

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
        } else {
          throw new Error("Could not identify new peers after creation");
        }
      } catch (e) {
        setStatus(String(e));
      }
    },
    [rpc],
  );

  /** Called after user actions to re-sync the full state. */
  const onAfterAction = useCallback(async () => {
    try {
      const statusResult = (await rpc.call("status", {})) as StatusResult;
      daemonStore.loadSnapshot(statusResult);
      setLastRefresh(new Date());
      setStatus("");
      await loadConnectionsDb();
      await loadDevicesRegistry();
    } catch (e) {
      setStatus(String(e));
    }
  }, [rpc, loadConnectionsDb, loadDevicesRegistry]);

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
      { k: "Version", v: String(daemonState.version || "—") },
    ];
  }, [daemonState.version, peers, edges.length]);

  const tabs: TabDef[] = [
    {
      id: "devices",
      label: "Devices",
      content: (
        <DevicesTab
          lastRefresh={lastRefresh}
          peers={peers}
          mdnsRemotes={mdnsParsed.remotes}
          alsaSeq={alsaSeq}
          rawmidi={rawmidi}
          alsaSubs={alsaSubs}
          registryDevices={registryDevices}
          registryEnabled={registryEnabled}
          connectionsDbEnabled={connectionsDbEnabled}
          highlightEndpointId={highlightEndpointId}
          rpc={rpc}
          onAfterAction={onAfterAction}
          onStatus={setStatus}
        />
      ),
    },
    {
      id: "connections",
      label: "Connections",
      content: (
        <ConnectionsTab
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
          registryDevices={registryDevices}
          registryEnabled={registryEnabled}
          onAfterAction={onAfterAction}
          onStatus={setStatus}
        />
      ),
    },
    {
      id: "peers",
      label: "Peers",
      content: (
        <PeersTab
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
          lastRefresh={lastRefresh}
          statsRows={statsRows}
        />
      ),
    },
    {
      id: "actions",
      label: "Actions",
      content: (
        <ActionsTab rpc={rpc} onRefresh={onAfterAction} onStatus={setStatus} />
      ),
    },
    {
      id: "settings",
      label: "Settings",
      content: (
        <SettingsTab
          theme={theme}
          setTheme={setTheme}
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
              Web control · JSON-RPC over WebSocket · Live updates
            </p>
            {daemonState.version && (
              <p class="mt-1 font-mono text-xs ui-text-subtle">
                Daemon{" "}
                <span class="font-bold ui-text">{daemonState.version}</span>
                {lastRefresh && (
                  <>
                    {" "}
                    · loaded{" "}
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
        <ConnectionBanner
          state={connState}
          onRetryNow={() => rpc.forceReconnect()}
        />
        {connState?.phase === "connected" && status ? (
          <p class="mb-4 font-mono text-xs ui-banner-warn">{status}</p>
        ) : null}
        <Tabs tabs={tabs} active={tab} onChange={setTab} />
      </div>
    </div>
  );
}
