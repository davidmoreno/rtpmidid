import { useCallback, useEffect, useRef, useState } from "preact/hooks";
import type { RpcClient } from "../rpc";
import { useEscapeKey } from "../useEscapeKey";
import { Button } from "./Button";
import { MidiMonitorPanel } from "./MidiMonitorPanel";

type Props = {
  identity: string;
  endpointLabel: string;
  rpc: RpcClient;
  onClose: () => void;
  onStatus: (msg: string) => void;
};

export function MidiMonitorModal({
  identity,
  endpointLabel,
  rpc,
  onClose,
  onStatus,
}: Props) {
  const [uuid, setUuid] = useState<string | null>(null);
  const uuidRef = useRef<string | null>(null);
  /** Guard to prevent double-calling monitor.stop (from stopSession + cleanup). */
  const stoppedRef = useRef(false);
  const [err, setErr] = useState<string>("");

  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        onStatus("");
        const r = (await rpc.call("monitor.start", {
          identity,
        })) as { uuid?: string };
        if (cancelled) return;
        if (typeof r?.uuid !== "string" || !r.uuid) {
          setErr("monitor.start did not return uuid");
          return;
        }
        setUuid(r.uuid);
        uuidRef.current = r.uuid;
      } catch (e) {
        if (!cancelled) setErr(String(e));
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [identity, rpc, onStatus]);

  useEffect(() => {
    return () => {
      const u = uuidRef.current;
      uuidRef.current = null;
      if (u && !stoppedRef.current)
        void rpc.call("monitor.stop", { uuid: u }).catch(() => {});
    };
  }, [rpc]);

  const stopSession = useCallback(async () => {
    const u = uuidRef.current;
    if (!u || stoppedRef.current) return;
    try {
      onStatus("");
      await rpc.call("monitor.stop", { uuid: u });
      stoppedRef.current = true;
    } catch (e) {
      onStatus(String(e));
      // Don't close the modal on error — let the user retry or see the message.
      return;
    }
    onClose();
  }, [rpc, onClose, onStatus]);

  const openInNewTab = useCallback(async () => {
    try {
      onStatus("");
      const r = (await rpc.call("monitor.start", {
        identity,
      })) as { uuid?: string };
      if (typeof r?.uuid !== "string" || !r.uuid) {
        onStatus("monitor.start did not return uuid");
        return;
      }
      const base = `${window.location.pathname}${window.location.search}`;
      window.open(`${base}#monitor?uuid=${encodeURIComponent(r.uuid)}`, "_blank");
    } catch (e) {
      onStatus(String(e));
    }
  }, [identity, rpc, onStatus]);

  const onMonitorSessionEnded = useCallback(() => {
    setUuid(null);
    setErr("Monitor session ended (connection closed).");
  }, []);

  useEscapeKey(stopSession);

  return (
    <div
      role="presentation"
      class="ui-modal-backdrop"
      onClick={() => void stopSession()}
    >
      <div
        role="dialog"
        aria-modal="true"
        aria-labelledby="midi-monitor-title"
        class="ui-modal flex max-h-[92vh] min-h-0 w-full max-w-3xl flex-col"
        onClick={(ev) => ev.stopPropagation()}
      >
        <h2
          id="midi-monitor-title"
          class="mb-2 shrink-0 font-mono text-sm font-bold uppercase ui-text"
        >
          MIDI monitor
        </h2>
        <p class="mb-3 shrink-0 font-mono text-[11px] leading-relaxed ui-text-muted">
          <span class="ui-text">{endpointLabel}</span>
          <span class="ui-text-subtle"> · </span>
          <span class="font-mono text-[10px]">{identity}</span>
        </p>

        {err ? (
          <p class="mb-3 shrink-0 font-mono text-[11px] text-red-600">{err}</p>
        ) : null}

        <div class="flex min-h-0 flex-1 flex-col overflow-hidden">
          {uuid ? (
            <MidiMonitorPanel
              uuid={uuid}
              className="min-h-0 flex-1"
              onSessionEnded={onMonitorSessionEnded}
            />
          ) : !err ? (
            <p class="py-8 text-center font-mono text-[11px] ui-text-muted">
              Starting session…
            </p>
          ) : null}
        </div>

        <div class="mt-4 flex shrink-0 flex-wrap gap-2">
          <Button type="button" onClick={() => void openInNewTab()}>
            Open in new tab
          </Button>
          <Button type="button" onClick={() => void stopSession()}>
            Stop monitor
          </Button>
        </div>
      </div>
    </div>
  );
}
