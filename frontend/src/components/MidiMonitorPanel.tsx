import { useEffect, useMemo, useRef, useState } from "preact/hooks";
import { MONITOR_HIGHLIGHT_MS, MONITOR_MAX_ROWS } from "../midiMonitorConstants";
import {
  createMidiParseBuffer,
  feedMidiBytes,
  formatMonitorClockMs,
  type MidiMonitorRowData,
} from "../midiMonitorParse";
import { monitorWebSocketUrl } from "../monitorWs";

export type MidiMonitorRow = MidiMonitorRowData & { id: number };

type Props = {
  uuid: string;
  /** Called when the binary WebSocket closes or errors */
  onConnectionChange?: (connected: boolean) => void;
  /** Once per mount when the socket closes (server removes the monitor session). */
  onSessionEnded?: () => void;
  className?: string;
};

export function MidiMonitorPanel({
  uuid,
  onConnectionChange,
  onSessionEnded,
  className,
}: Props) {
  const [rows, setRows] = useState<MidiMonitorRow[]>([]);
  const [highlighted, setHighlighted] = useState<Set<number>>(() => new Set());
  const parseBuf = useMemo(() => createMidiParseBuffer(), [uuid]);
  const nextId = useRef(1);
  const wsRef = useRef<WebSocket | null>(null);
  /** Avoid effect teardown when parent passes an inline `onConnectionChange` each render. */
  const onConnRef = useRef(onConnectionChange);
  onConnRef.current = onConnectionChange;
  const onEndedRef = useRef(onSessionEnded);
  onEndedRef.current = onSessionEnded;

  useEffect(() => {
    let cancelled = false;
    let socket: WebSocket | null = null;
    let retryTimer: ReturnType<typeof setTimeout> | undefined;
    let retryAttempt = 0;
    const maxRetries = 8;

    const attachHandlers = (ws: WebSocket) => {
      ws.onopen = () => {
        retryAttempt = 0;
        console.info("[rtpmidid:monitor] WebSocket open", monitorWebSocketUrl(uuid));
        onConnRef.current?.(true);
      };
      ws.onclose = (ev) => {
        wsRef.current = null;
        console.info(
          "[rtpmidid:monitor] WebSocket close",
          "code=",
          ev.code,
          "reason=",
          ev.reason || "(empty)",
          "wasClean=",
          ev.wasClean,
        );
        onConnRef.current?.(false);
        const retriableUnknown =
          ev.code === 1007 &&
          (ev.reason.includes("unknown session") ||
            ev.reason.includes("need ?uuid="));
        if (!cancelled && retriableUnknown && retryAttempt < maxRetries) {
          retryAttempt += 1;
          const delayMs = 50 * retryAttempt;
          console.info(
            "[rtpmidid:monitor] retrying connection in",
            delayMs,
            "ms (attempt",
            retryAttempt,
            "of",
            maxRetries,
            ")",
          );
          retryTimer = window.setTimeout(() => {
            if (!cancelled) openSocket();
          }, delayMs);
          return;
        }
        if (!cancelled) onEndedRef.current?.();
      };
      ws.onerror = () => {
        console.warn(
          "[rtpmidid:monitor] WebSocket error (Firefox often fires before close; see close event)",
        );
      };

      ws.onmessage = (ev) => {
        const ab = ev.data as ArrayBuffer;
        const u8 = new Uint8Array(ab);
        feedMidiBytes(parseBuf, u8, (data) => {
          const id = nextId.current++;
          setRows((prev) => {
            const row: MidiMonitorRow = { ...data, id };
            const next = [row, ...prev];
            return next.length > MONITOR_MAX_ROWS
              ? next.slice(0, MONITOR_MAX_ROWS)
              : next;
          });
          setHighlighted((h) => new Set(h).add(id));
          window.setTimeout(() => {
            setHighlighted((h) => {
              const n = new Set(h);
              n.delete(id);
              return n;
            });
          }, MONITOR_HIGHLIGHT_MS);
        });
      };
    };

    const openSocket = () => {
      if (cancelled) return;
      const url = monitorWebSocketUrl(uuid);
      console.info("[rtpmidid:monitor] connecting", url);
      socket = new WebSocket(url);
      socket.binaryType = "arraybuffer";
      wsRef.current = socket;
      attachHandlers(socket);
    };

    openSocket();

    return () => {
      cancelled = true;
      if (retryTimer !== undefined) window.clearTimeout(retryTimer);
      socket?.close();
      wsRef.current = null;
    };
  }, [uuid, parseBuf]);

  return (
    <div
      class={`flex min-h-0 flex-1 flex-col ${className ?? ""}`}
    >
      <div class="min-h-0 flex-1 overflow-auto rounded-[var(--radius-md)] border border-[color:var(--color-border)]">
      <table class="w-full border-collapse font-mono text-[11px]">
        <thead class="sticky top-0 bg-[color:var(--color-surface-2)]">
          <tr class="text-left uppercase ui-text-muted">
            <th
              class="px-2 py-1 font-black"
              title="Monotonic time since page load (MM:SS.mmm, or H:MM:SS.mmm if ≥ 1h)"
            >
              Time
            </th>
            <th class="px-2 py-1 font-black">Message</th>
            <th class="px-2 py-1 font-black">Ch</th>
            <th class="px-2 py-1 font-black">Hex</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((r) => (
            <tr
              key={r.id}
              class={
                highlighted.has(r.id)
                  ? "ui-monitor-row-highlight bg-[color:var(--color-surface-2)]"
                  : "odd:bg-[color:var(--color-surface-zebra-a)]"
              }
            >
              <td class="px-2 py-0.5 tabular-nums ui-text-subtle">
                {formatMonitorClockMs(r.atMs)}
              </td>
              <td class="px-2 py-0.5 ui-text">{r.label}</td>
              <td class="px-2 py-0.5 ui-text-muted">
                {r.channel !== null ? String(r.channel) : "—"}
              </td>
              <td class="max-w-[14rem] truncate px-2 py-0.5 text-[10px] ui-text-subtle">
                {r.hex}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      {rows.length === 0 ? (
        <div class="px-3 py-6 text-center font-mono text-[11px] ui-text-subtle">
          Waiting for MIDI…
        </div>
      ) : null}
      </div>
    </div>
  );
}
