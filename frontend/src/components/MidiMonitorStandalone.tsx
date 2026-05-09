import { useCallback } from "preact/hooks";
import type { RpcClient } from "../rpc";
import { Button } from "./Button";
import { MidiMonitorPanel } from "./MidiMonitorPanel";

type Props = {
  uuid: string;
  rpc: RpcClient;
  onExit: () => void;
  onStatus: (msg: string) => void;
};

export function MidiMonitorStandalone({
  uuid,
  rpc,
  onExit,
  onStatus,
}: Props) {
  const stop = useCallback(async () => {
    try {
      onStatus("");
      await rpc.call("monitor.stop", { uuid });
    } catch (e) {
      onStatus(String(e));
    }
    onExit();
  }, [uuid, rpc, onExit, onStatus]);

  const onSessionEnded = useCallback(() => {
    onStatus("Monitor session ended.");
    onExit();
  }, [onExit, onStatus]);

  return (
    <div class="ui-page flex min-h-screen flex-col">
      <div class="mx-auto flex w-full max-w-5xl flex-1 flex-col gap-3 px-4 py-4 min-h-0">
        <header class="shrink-0">
          <h1 class="font-mono text-xl font-black uppercase tracking-tight ui-text">
            MIDI monitor
          </h1>
          <p class="mt-1 font-mono text-[11px] ui-text-muted">
            Session <span class="font-bold ui-text">{uuid}</span>
          </p>
        </header>
        <MidiMonitorPanel
          uuid={uuid}
          className="min-h-0 flex-1"
          onSessionEnded={onSessionEnded}
        />
        <div class="flex shrink-0 flex-wrap gap-2">
          <Button type="button" onClick={() => void stop()}>
            Stop monitor
          </Button>
        </div>
      </div>
    </div>
  );
}
