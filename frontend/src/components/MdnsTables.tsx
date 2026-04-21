import { useEffect, useState } from "preact/hooks";
import {
  groupMdnsAnnouncements,
  groupMdnsRemotes,
  type MdnsAnnouncement,
  type MdnsAnnouncementGroup,
  type MdnsRemote,
  type MdnsRemoteGroup,
} from "../model";
import {
  parseMidiAlsaSeqResult,
  parseMidiRawmidiResult,
  type MidiAlsaSeqEntry,
  type MidiRawmidiEntry,
  type WireLocalChoice,
} from "../midiEnumerate";
import type { RpcClient } from "../rpc";
import { Button } from "./Button";
import { useFixedTooltip } from "./FixedTooltipPortal";

type Props = {
  status: string;
  announcements: MdnsAnnouncement[];
  remotes: MdnsRemote[];
  rpc: RpcClient;
  /**
   * Creates a new `local_alsa_peer_t` or `local_rawmidi_t` from the chosen listing,
   * adds `network_rtpmidi_client_t`, and bidirectional `router.connect` edges.
   */
  onWireMdnsToLocal?: (args: {
    serviceName: string;
    target: string;
    port: number | string;
    local: WireLocalChoice;
  }) => Promise<void>;
};

/** Host:port or [ipv6]:port for display / copy. */
function connectEndpoint(ip: string, port: number | string): string {
  const p = String(port);
  const trimmed = ip.trim();
  if (!trimmed) return "";
  if (trimmed.includes(":") && !trimmed.startsWith("[")) {
    return `[${trimmed}]:${p}`;
  }
  return `${trimmed}:${p}`;
}

function remoteGroupKey(g: MdnsRemoteGroup): string {
  return `${g.name}\0${String(g.port)}`;
}

function announcementTipContent(a: MdnsAnnouncementGroup) {
  return (
    <div class="space-y-1 text-zinc-800 dark:text-zinc-100">
      <div class="font-bold uppercase text-zinc-500 dark:text-zinc-400">
        Announcement
      </div>
      <div>
        <span class="text-zinc-500">Name:</span> {a.name}
      </div>
      <div>
        <span class="text-zinc-500">Port:</span> {a.port}
      </div>
      <div>
        <span class="text-zinc-500">Merged rows:</span> <strong>{a.count}</strong>
      </div>
    </div>
  );
}

function ipListForGroup(g: MdnsRemoteGroup): string[] {
  if (g.ips.length > 0) return g.ips;
  const seen = new Set<string>();
  const out: string[] = [];
  for (const inst of g.instances) {
    const ip = inst.ip.trim();
    if (ip && !seen.has(ip)) {
      seen.add(ip);
      out.push(ip);
    }
  }
  return out;
}

function parseEndpointOption(
  v: string,
  alsaSeq: MidiAlsaSeqEntry[],
  rawmidi: MidiRawmidiEntry[],
): WireLocalChoice | null {
  if (!v) return null;
  if (v.startsWith("alsa:")) {
    const id = decodeURIComponent(v.slice(5));
    const entry = alsaSeq.find((e) => e.id === id);
    if (!entry) return null;
    return {
      mode: "alsa_seq",
      listingLabel: entry.label,
      client: entry.client,
      port: entry.port,
    };
  }
  if (v.startsWith("raw:")) {
    const device = decodeURIComponent(v.slice(4));
    const entry = rawmidi.find((e) => e.device === device);
    if (!entry) return null;
    return {
      mode: "rawmidi",
      device: entry.device,
      listingLabel: entry.label,
    };
  }
  return null;
}

type ConnectDialogState = {
  serviceName: string;
  target: string;
  port: number | string;
  busyKey: string;
};

export function MdnsTables({
  status,
  announcements,
  remotes,
  rpc,
  onWireMdnsToLocal,
}: Props) {
  const annGroups = groupMdnsAnnouncements(announcements);
  const remoteGroups = groupMdnsRemotes(remotes);
  const tt = useFixedTooltip();

  const [selectedRemoteKey, setSelectedRemoteKey] = useState<string | null>(null);
  const [connectBusyKey, setConnectBusyKey] = useState<string | null>(null);
  const [connectDialog, setConnectDialog] = useState<ConnectDialogState | null>(
    null,
  );
  const [alsaSeq, setAlsaSeq] = useState<MidiAlsaSeqEntry[]>([]);
  const [rawmidi, setRawmidi] = useState<MidiRawmidiEntry[]>([]);
  const [midiLoading, setMidiLoading] = useState(false);
  const [alsaListErr, setAlsaListErr] = useState<string | null>(null);
  const [rawListErr, setRawListErr] = useState<string | null>(null);
  const [endpointChoice, setEndpointChoice] = useState("");
  const [dialogSubmitting, setDialogSubmitting] = useState(false);

  const selectedRemote =
    selectedRemoteKey === null
      ? null
      : remoteGroups.find((g) => remoteGroupKey(g) === selectedRemoteKey) ?? null;

  useEffect(() => {
    if (selectedRemoteKey === null) return;
    const groups = groupMdnsRemotes(remotes);
    if (!groups.some((g) => remoteGroupKey(g) === selectedRemoteKey)) {
      setSelectedRemoteKey(null);
    }
  }, [remotes, selectedRemoteKey]);

  useEffect(() => {
    if (!connectDialog) return;
    setEndpointChoice("");
    setAlsaSeq([]);
    setRawmidi([]);
    setAlsaListErr(null);
    setRawListErr(null);
    setMidiLoading(true);
    let cancelled = false;
    (async () => {
      let alsaErr: string | null = null;
      let rawErr: string | null = null;
      let alsaList: MidiAlsaSeqEntry[] = [];
      let rawList: MidiRawmidiEntry[] = [];
      try {
        const rAlsa = await rpc.call("midi.listAlsaSeq", {});
        if (cancelled) return;
        if (
          rAlsa &&
          typeof rAlsa === "object" &&
          "error" in rAlsa &&
          !Array.isArray(rAlsa)
        ) {
          alsaErr = String((rAlsa as { error: unknown }).error);
        } else {
          alsaList = parseMidiAlsaSeqResult(rAlsa) ?? [];
        }
      } catch (e) {
        alsaErr = String(e);
      }
      try {
        const rRaw = await rpc.call("midi.listRawMidi", {});
        if (cancelled) return;
        rawList = parseMidiRawmidiResult(rRaw) ?? [];
      } catch (e) {
        rawErr = String(e);
      }
      if (cancelled) return;
      setAlsaSeq(alsaList);
      setRawmidi(rawList);
      setAlsaListErr(alsaErr);
      setRawListErr(rawErr);
      setMidiLoading(false);
    })();
    return () => {
      cancelled = true;
    };
  }, [connectDialog, rpc]);

  useEffect(() => {
    if (!connectDialog) return undefined;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") setConnectDialog(null);
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [connectDialog]);

  const openConnectDialog = (
    busyKey: string,
    serviceName: string,
    target: string,
    port: number | string,
  ) => {
    if (!onWireMdnsToLocal || !target.trim()) return;
    setConnectDialog({ serviceName, target: target.trim(), port, busyKey });
  };

  const hasMidiEndpoints =
    !midiLoading && (alsaSeq.length > 0 || rawmidi.length > 0);

  const confirmWire = async () => {
    if (!connectDialog || !onWireMdnsToLocal) return;
    const local = parseEndpointOption(endpointChoice, alsaSeq, rawmidi);
    if (!local) return;
    setDialogSubmitting(true);
    setConnectBusyKey(connectDialog.busyKey);
    try {
      await onWireMdnsToLocal({
        serviceName: connectDialog.serviceName,
        target: connectDialog.target,
        port: connectDialog.port,
        local,
      });
      setConnectDialog(null);
    } finally {
      setDialogSubmitting(false);
      setConnectBusyKey(null);
    }
  };

  return (
    <div class="space-y-6">
      <div class="grid gap-4 lg:grid-cols-2">
        <div>
          <p class="mb-2 font-mono text-xs font-bold uppercase text-zinc-600 dark:text-zinc-400">
            mDNS status:{" "}
            <span class="text-zinc-900 dark:text-zinc-100">{status}</span>
          </p>
          <h3 class="mb-1 font-mono text-xs font-bold uppercase">
            Local announcements
          </h3>
          <p class="mb-1 font-mono text-[10px] text-zinc-500">
            Grouped by name and port. Hover a row for merged count and details
            (fixed layer, not clipped).
          </p>
          <div class="overflow-x-auto border-2 border-zinc-900 dark:border-zinc-100">
            <table class="w-full border-collapse text-left font-mono text-xs">
              <thead>
                <tr class="bg-zinc-200 dark:bg-zinc-800">
                  <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                    Name
                  </th>
                  <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                    Port
                  </th>
                  <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                    #
                  </th>
                </tr>
              </thead>
              <tbody>
                {annGroups.length === 0 ? (
                  <tr>
                    <td colSpan={3} class="px-2 py-2 text-zinc-500">
                      None
                    </td>
                  </tr>
                ) : (
                  annGroups.map((a) => (
                    <tr
                      key={`${a.name}-${String(a.port)}`}
                      class="cursor-default border-b border-zinc-200 odd:bg-white even:bg-zinc-50 dark:border-zinc-700 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80"
                      onMouseEnter={(e) =>
                        tt.show(e.currentTarget as HTMLElement, announcementTipContent(a))
                      }
                      onMouseLeave={tt.scheduleHide}
                    >
                      <td class="max-w-[16rem] truncate px-2 py-1">{a.name}</td>
                      <td class="px-2 py-1 tabular-nums">{a.port}</td>
                      <td class="px-2 py-1 tabular-nums">{a.count}</td>
                    </tr>
                  ))
                )}
              </tbody>
            </table>
          </div>
        </div>
        <div>
          <h3 class="mb-1 font-mono text-xs font-bold uppercase">
            Discovered remotes
          </h3>
          <p class="mb-1 font-mono text-[10px] text-zinc-500">
            Grouped by service name and port. Click a row for resolved IPs and
            Connect (pick ALSA sequencer port or raw MIDI device from the daemon
            list).
          </p>
          <div class="overflow-x-auto border-2 border-zinc-900 dark:border-zinc-100">
            <table class="w-full border-collapse text-left font-mono text-xs">
              <thead>
                <tr class="bg-zinc-200 dark:bg-zinc-800">
                  <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                    Name
                  </th>
                  <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                    Port
                  </th>
                  <th class="border-b border-zinc-900 px-2 py-1 dark:border-zinc-100">
                    IPs
                  </th>
                </tr>
              </thead>
              <tbody>
                {remoteGroups.length === 0 ? (
                  <tr>
                    <td colSpan={3} class="px-2 py-2 text-zinc-500">
                      None
                    </td>
                  </tr>
                ) : (
                  remoteGroups.map((g) => {
                    const rk = remoteGroupKey(g);
                    const sel = selectedRemoteKey === rk;
                    return (
                      <tr
                        key={rk}
                        role="button"
                        tabIndex={0}
                        aria-selected={sel}
                        class={`cursor-pointer border-b border-zinc-200 dark:border-zinc-700 ${
                          sel
                            ? "bg-amber-100 ring-2 ring-inset ring-amber-700 hover:bg-amber-200 dark:bg-amber-950/50 dark:ring-amber-400 dark:hover:bg-amber-900/60"
                            : "odd:bg-white even:bg-zinc-50 hover:bg-zinc-100 dark:odd:bg-zinc-950 dark:even:bg-zinc-900/80 dark:hover:bg-zinc-800/90"
                        }`}
                        onClick={() =>
                          setSelectedRemoteKey((k) => (k === rk ? null : rk))
                        }
                        onKeyDown={(e) => {
                          if (e.key === "Enter" || e.key === " ") {
                            e.preventDefault();
                            setSelectedRemoteKey((k) => (k === rk ? null : rk));
                          }
                        }}
                      >
                        <td class="max-w-[14rem] truncate px-2 py-1">{g.name}</td>
                        <td class="px-2 py-1 tabular-nums">{g.port}</td>
                        <td class="px-2 py-1 tabular-nums">
                          {g.ips.length > 0 ? g.ips.length : g.instances.length}
                        </td>
                      </tr>
                    );
                  })
                )}
              </tbody>
            </table>
          </div>
        </div>
        {tt.portal}
      </div>

      {selectedRemote && (
        <div class="border-2 border-zinc-900 bg-zinc-50 p-4 font-mono dark:border-zinc-100 dark:bg-zinc-900/50">
          <h3 class="mb-3 text-sm font-bold uppercase text-zinc-800 dark:text-zinc-100">
            Details — {selectedRemote.name}{" "}
            <span class="text-zinc-500">· port {selectedRemote.port}</span>
          </h3>

          <div class="space-y-4 text-xs text-zinc-800 dark:text-zinc-100">
            <section>
              <h4 class="mb-2 text-[11px] font-bold uppercase text-emerald-800 dark:text-emerald-300">
                Resolved IPs
              </h4>
              <p class="mb-2 text-[10px] text-zinc-600 dark:text-zinc-400">
                Connect lists ALSA sequencer ports and /dev/snd raw MIDI devices
                from the host, creates a matching local peer, then adds an
                RTP-MIDI client to this address.
              </p>
              {ipListForGroup(selectedRemote).length === 0 ? (
                <p class="text-[10px] text-amber-800 dark:text-amber-300">
                  No resolved IP in status JSON. Use hostname connect below or
                  check daemon mDNS IP fields.
                </p>
              ) : (
                <ul class="space-y-2">
                  {ipListForGroup(selectedRemote).map((ip) => {
                    const bk = `ip:${ip}:${String(selectedRemote.port)}`;
                    return (
                      <li
                        key={ip}
                        class="flex flex-wrap items-center justify-between gap-2 border-b border-zinc-200 pb-2 last:border-0 dark:border-zinc-700"
                      >
                        <span class="break-all font-mono text-[11px]">
                          <strong>{connectEndpoint(ip, selectedRemote.port)}</strong>
                        </span>
                        {onWireMdnsToLocal ? (
                          <Button
                            disabled={
                              connectDialog !== null || connectBusyKey !== null
                            }
                            onClick={() =>
                              openConnectDialog(
                                bk,
                                selectedRemote.name,
                                ip,
                                selectedRemote.port,
                              )
                            }
                          >
                            Connect
                          </Button>
                        ) : null}
                      </li>
                    );
                  })}
                </ul>
              )}
            </section>

            <section>
              <h4 class="mb-2 text-[11px] font-bold uppercase text-amber-800 dark:text-amber-300">
                Hostname (mDNS / DNS)
              </h4>
              <p class="mb-2 text-[10px] text-zinc-600 dark:text-zinc-400">
                Same as IP connect: choose an ALSA or raw MIDI endpoint from the
                host list, then wire an RTP-MIDI client using this hostname.
              </p>
              {selectedRemote.addresses.length === 0 ? (
                <p class="text-[10px] text-zinc-500">No mDNS hostnames in status.</p>
              ) : (
                <ul class="space-y-2">
                  {selectedRemote.addresses.map((h) => {
                    const bk = `host:${h}:${String(selectedRemote.port)}`;
                    return (
                      <li
                        key={h}
                        class="flex flex-wrap items-center justify-between gap-2 border-b border-zinc-200 pb-2 last:border-0 dark:border-zinc-700"
                      >
                        <span class="break-all font-mono text-[11px]">{h}</span>
                        {onWireMdnsToLocal ? (
                          <Button
                            disabled={
                              connectDialog !== null || connectBusyKey !== null
                            }
                            onClick={() =>
                              openConnectDialog(
                                bk,
                                selectedRemote.name,
                                h,
                                selectedRemote.port,
                              )
                            }
                          >
                            Connect
                          </Button>
                        ) : null}
                      </li>
                    );
                  })}
                </ul>
              )}
            </section>
          </div>
        </div>
      )}

      {connectDialog && (
        <div
          role="presentation"
          class="fixed inset-0 z-[100] flex items-center justify-center bg-black/50 p-4"
          onClick={() => !dialogSubmitting && setConnectDialog(null)}
        >
          <div
            role="dialog"
            aria-modal="true"
            aria-labelledby="mdns-wire-title"
            class="max-h-[90vh] w-full max-w-lg overflow-y-auto border-2 border-zinc-900 bg-white p-4 shadow-xl dark:border-zinc-100 dark:bg-zinc-950"
            onClick={(e) => e.stopPropagation()}
          >
            <h2
              id="mdns-wire-title"
              class="font-mono text-sm font-bold uppercase text-zinc-900 dark:text-zinc-100"
            >
              Wire remote to local MIDI
            </h2>
            <p class="mt-2 font-mono text-[11px] leading-relaxed text-zinc-700 dark:text-zinc-300">
              Remote service{" "}
              <strong>{connectDialog.serviceName}</strong>
              {" "}via{" "}
              <strong class="break-all">{connectDialog.target}</strong>:
              {String(connectDialog.port)} — creates an RTP-MIDI client and
              bidirectional router links with the peer you select.
            </p>

            <div class="mt-4">
              <label class="mb-2 block font-mono text-[10px] font-bold uppercase text-zinc-600 dark:text-zinc-400">
                Local endpoint
              </label>
              {midiLoading ? (
                <p class="font-mono text-[10px] text-zinc-600 dark:text-zinc-400">
                  Loading ALSA sequencer ports and raw MIDI devices…
                </p>
              ) : (
                <>
                  {(alsaListErr || rawListErr) && (
                    <div class="mb-2 space-y-1 font-mono text-[10px] text-amber-800 dark:text-amber-300">
                      {alsaListErr ? (
                        <div>
                          ALSA list: <span class="break-all">{alsaListErr}</span>
                        </div>
                      ) : null}
                      {rawListErr ? (
                        <div>
                          Raw MIDI list:{" "}
                          <span class="break-all">{rawListErr}</span>
                        </div>
                      ) : null}
                    </div>
                  )}
                  {!hasMidiEndpoints ? (
                    <p class="font-mono text-[10px] text-amber-800 dark:text-amber-300">
                      No ALSA sequencer ports or raw MIDI devices reported. Check
                      ALSA permissions, hardware, and{" "}
                      <code class="rounded bg-zinc-200 px-0.5 dark:bg-zinc-800">
                        /dev/snd/
                      </code>
                      .
                    </p>
                  ) : (
                    <select
                      class="w-full border-2 border-zinc-900 bg-white px-2 py-1 font-mono text-[11px] dark:border-zinc-100 dark:bg-zinc-950"
                      value={endpointChoice}
                      onInput={(e) =>
                        setEndpointChoice((e.target as HTMLSelectElement).value)
                      }
                    >
                      <option value="">Choose ALSA seq or raw MIDI…</option>
                      {alsaSeq.length > 0 ? (
                        <optgroup label="ALSA sequencer (exported ports)">
                          {alsaSeq.map((e) => (
                            <option
                              key={`alsa:${e.id}`}
                              value={`alsa:${encodeURIComponent(e.id)}`}
                            >
                              {e.label} · {e.kind} · {e.id}
                            </option>
                          ))}
                        </optgroup>
                      ) : null}
                      {rawmidi.length > 0 ? (
                        <optgroup label="Raw MIDI">
                          {rawmidi.map((e) => (
                            <option
                              key={`raw:${e.device}`}
                              value={`raw:${encodeURIComponent(e.device)}`}
                            >
                              {e.label} · {e.device}
                            </option>
                          ))}
                        </optgroup>
                      ) : null}
                    </select>
                  )}
                </>
              )}
            </div>

            <div class="mt-6 flex flex-wrap gap-2">
              <Button
                disabled={dialogSubmitting}
                onClick={() => !dialogSubmitting && setConnectDialog(null)}
              >
                Cancel
              </Button>
              <Button
                disabled={
                  dialogSubmitting ||
                  midiLoading ||
                  !hasMidiEndpoints ||
                  endpointChoice === "" ||
                  parseEndpointOption(endpointChoice, alsaSeq, rawmidi) === null
                }
                onClick={() => void confirmWire()}
              >
                {dialogSubmitting ? "…" : "Connect"}
              </Button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
