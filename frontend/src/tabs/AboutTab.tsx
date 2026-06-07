import { useCallback, useRef, useState } from "preact/hooks";
import { Card } from "../components/Card";
import { RefreshBanner } from "../components/RefreshBanner";
import { StatTable } from "../components/StatTable";

type Props = {
  lastRefresh: Date | null;
  statsRows: { k: string; v: string }[];
  webUrl: string;
  webAccessible: boolean;
};

export function AboutTab({
  lastRefresh,
  statsRows,
  webUrl,
  webAccessible,
}: Props) {
  const [copied, setCopied] = useState(false);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const handleCopy = useCallback(() => {
    navigator.clipboard.writeText(webUrl).then(() => {
      setCopied(true);
      if (timerRef.current) clearTimeout(timerRef.current);
      timerRef.current = setTimeout(() => setCopied(false), 2000);
    });
  }, [webUrl]);

  return (
    <div class="space-y-4">
      <RefreshBanner lastRefresh={lastRefresh} />

      {webAccessible && (
        <Card title="Access from other devices">
          <p class="mb-2 font-mono text-xs ui-text-muted">
            Open this URL on any device on the same network to manage{" "}
            <strong class="ui-text">rtpmidid</strong> remotely.
          </p>
          <div class="flex items-center gap-2">
            <code class="ui-code flex-1 break-all text-sm py-2 px-3 rounded bg-black/20">
              {webUrl}
            </code>
            <button
              class={`ui-btn flex-shrink-0 text-xs px-3 py-2 min-w-[5rem] ${
                copied ? "ui-btn-success" : ""
              }`}
              onClick={handleCopy}
            >
              {copied ? "✓ Copied" : "Copy"}
            </button>
          </div>
        </Card>
      )}

      <Card title="About this UI">
        <p class="mb-3 font-mono text-xs leading-relaxed ui-text-muted">
          Web dashboard for <strong class="ui-text">rtpmidid</strong>: live
          connections, router peers, RTP view, and mDNS discovery. Commands are sent
          over JSON-RPC on a WebSocket to the daemon.
        </p>
        <StatTable rows={statsRows} />
        <p class="mt-3 font-mono text-xs ui-text-muted">
          Latency bars use a shared piecewise scale and color tiers in{" "}
          <code class="ui-code">latencyScale.ts</code>. Each row shows one bar for
          the <strong class="ui-text">sum</strong> of available samples; hover the bar
          for a per-metric breakdown.
        </p>
      </Card>
    </div>
  );
}
