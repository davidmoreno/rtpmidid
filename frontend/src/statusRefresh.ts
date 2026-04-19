export const STORAGE_KEY_STATUS_REFRESH_MS = "rtpmidid-status-refresh-ms";

/** Default before any user preference (matches prior hard-coded behavior). */
export const DEFAULT_STATUS_REFRESH_MS = 5000;

export const STATUS_REFRESH_CHOICES = [
  { ms: 0, label: "Off" },
  { ms: 500, label: "0.5s" },
  { ms: 1000, label: "1s" },
  { ms: 5000, label: "5s" },
] as const;

const ALLOWED_MS = new Set(STATUS_REFRESH_CHOICES.map((c) => c.ms));

export function parseStoredStatusRefreshMs(raw: string | null): number {
  if (!raw) return DEFAULT_STATUS_REFRESH_MS;
  const n = Number(raw);
  return ALLOWED_MS.has(n) ? n : DEFAULT_STATUS_REFRESH_MS;
}
