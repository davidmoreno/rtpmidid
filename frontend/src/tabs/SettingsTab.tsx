import { Card } from "../components/Card";
import { THEME_OPTIONS, type ThemeId } from "../theme";

type Props = {
  theme: ThemeId;
  setTheme: (t: ThemeId) => void;
};

export function SettingsTab({ theme, setTheme }: Props) {
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
    </div>
  );
}
