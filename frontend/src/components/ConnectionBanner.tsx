import type { RpcConnectionState } from "../rpc";
import { formatRetryCountdown } from "../rpc";

type Props = {
  state: RpcConnectionState | null;
  onRetryNow: () => void;
};

export function ConnectionBanner({ state, onRetryNow }: Props) {
  if (!state || state.phase === "connected") return null;

  const showRetryNow =
    state.phase === "waiting_retry" ||
    state.phase === "connecting" ||
    state.phase === "authenticating" ||
    state.phase === "disconnected";

  if (state.phase === "auth_failed") {
    return (
      <div
        class="ui-conn-banner ui-conn-banner-error mb-4"
        role="status"
        aria-live="polite"
      >
        <div class="ui-conn-banner-body">
          <span class="ui-conn-banner-title">Authentication failed</span>
          <span class="ui-conn-banner-detail">
            {state.detail ?? "Check Web UI credentials in Actions and reload."}
          </span>
        </div>
      </div>
    );
  }

  const retryLabel =
    state.phase === "waiting_retry" && state.retryInMs !== undefined
      ? formatRetryCountdown(state.retryInMs)
      : null;

  let title = "Connecting to daemon…";
  if (state.phase === "waiting_retry") {
    title =
      retryLabel !== null
        ? `Not connected · next retry in ${retryLabel}`
        : "Not connected · retry pending…";
  } else if (state.phase === "authenticating") {
    title = "Authenticating…";
  } else if (state.phase === "disconnected") {
    title = "Disconnected";
  }

  return (
    <div
      class="ui-conn-banner mb-4"
      role="status"
      aria-live="polite"
      aria-atomic="true"
    >
      <div class="ui-conn-banner-body">
        <span class="ui-conn-banner-title">{title}</span>
        <span class="ui-conn-banner-detail">
          {state.detail ?? state.url}
          {state.attempt > 1 ? ` · attempt ${state.attempt}` : ""}
        </span>
      </div>
      {showRetryNow ? (
        <button
          type="button"
          class="ui-conn-banner-retry"
          onClick={() => onRetryNow()}
        >
          Retry now
        </button>
      ) : null}
    </div>
  );
}
