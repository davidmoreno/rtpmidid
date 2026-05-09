import { STATUS_REFRESH_CHOICES } from "../statusRefresh";

type Props = {
  refreshIntervalMs: number;
  lastRefresh: Date | null;
};

export function RefreshBanner({ refreshIntervalMs, lastRefresh }: Props) {
  const choice =
    STATUS_REFRESH_CHOICES.find((c) => c.ms === refreshIntervalMs)?.label ??
    `${refreshIntervalMs / 1000}s`;

  return (
    <div class="flex flex-wrap items-center justify-between gap-2 font-mono text-xs ui-text-muted">
      <span>
        {refreshIntervalMs <= 0 ? (
          <>
            Automatic polling{" "}
            <strong class="ui-text">off</strong> — data updates only after Actions
            commands or reloading.
          </>
        ) : (
          <>
            Poll every <strong class="ui-text">{choice}</strong> after each status
            response on this tab.
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
}
