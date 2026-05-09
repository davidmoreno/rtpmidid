import { Card } from "../components/Card";
import {
  STATUS_REFRESH_CHOICES,
  STORAGE_KEY_STATUS_REFRESH_MS,
} from "../statusRefresh";
import { THEME_OPTIONS, type ThemeId } from "../theme";

type Props = {
  theme: ThemeId;
  setTheme: (t: ThemeId) => void;
  refreshIntervalMs: number;
  setRefreshIntervalMs: (ms: number) => void;
};

export function SettingsTab({
  theme,
  setTheme,
  refreshIntervalMs,
  setRefreshIntervalMs,
}: Props) {
  return (
    <div class="grid max-w-xl gap-4">
      <Card title="Appearance">
        <label class="block font-mono text-xs ui-text-muted">
          Theme
          <select
            class="ui-select mt-2 font-mono text-sm"
            value={theme}
            onChange={(e) =>
              setTheme((e.target as HTMLSelectElement).value as ThemeId)
            }
          >
            {THEME_OPTIONS.map((o) => (
              <option key={o.id} value={o.id}>
                {o.label}
              </option>
            ))}
          </select>
        </label>
      </Card>
      <Card title="Status polling">
        <p class="mb-2 font-mono text-xs ui-text-muted">
          How often to request a new <code class="ui-code">status</code> after the
          previous response completes (on tabs that auto-refresh).
        </p>
        <label class="flex flex-wrap items-center gap-2 font-mono text-xs ui-text-muted">
          <span class="whitespace-nowrap">Interval</span>
          <select
            class="ui-select mt-0 max-w-xs font-mono text-sm"
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
      </Card>
    </div>
  );
}
