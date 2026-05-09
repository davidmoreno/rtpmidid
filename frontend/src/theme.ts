import { useEffect, useState } from "preact/hooks";

export const STORAGE_KEY_UI_THEME = "rtpmidid-ui-theme";

export type ThemeId =
  | "light-brutalist"
  | "dark-brutalist"
  | "light-normal"
  | "dark-normal"
  | "light-cute-pastel"
  | "dark-cute-pastel";

export const THEME_OPTIONS: { id: ThemeId; label: string }[] = [
  { id: "light-brutalist", label: "Light brutalist" },
  { id: "dark-brutalist", label: "Dark brutalist" },
  { id: "light-normal", label: "Light normal" },
  { id: "dark-normal", label: "Dark normal" },
  { id: "light-cute-pastel", label: "Light cute pastel" },
  { id: "dark-cute-pastel", label: "Dark cute pastel" },
];

export const DEFAULT_THEME: ThemeId = "light-brutalist";

export function isThemeId(s: string | null): s is ThemeId {
  return s !== null && THEME_OPTIONS.some((t) => t.id === s);
}

export function parseStoredTheme(): ThemeId {
  if (typeof localStorage === "undefined") return DEFAULT_THEME;
  const raw = localStorage.getItem(STORAGE_KEY_UI_THEME);
  return isThemeId(raw) ? raw : DEFAULT_THEME;
}

/** Sync DOM + localStorage (call when user selects a theme). */
export function applyTheme(id: ThemeId): void {
  document.documentElement.dataset.theme = id;
  localStorage.setItem(STORAGE_KEY_UI_THEME, id);
}

/** Reactive theme state; applies on change and matches stored value on mount. */
export function useUiTheme(): [ThemeId, (id: ThemeId) => void] {
  const [theme, setTheme] = useState<ThemeId>(() => parseStoredTheme());
  useEffect(() => {
    applyTheme(theme);
  }, [theme]);
  return [theme, setTheme];
}
