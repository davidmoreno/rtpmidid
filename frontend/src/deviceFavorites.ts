/** Browser-local favorites for Devices (endpoint ids). */

export const STORAGE_KEY_DEVICE_FAVORITES = "rtpmidid-ui-device-favorites";

export function loadDeviceFavoriteIds(): Set<string> {
  if (typeof localStorage === "undefined") return new Set();
  try {
    const raw = localStorage.getItem(STORAGE_KEY_DEVICE_FAVORITES);
    if (!raw) return new Set();
    const parsed = JSON.parse(raw) as unknown;
    if (!Array.isArray(parsed)) return new Set();
    return new Set(parsed.filter((x): x is string => typeof x === "string"));
  } catch {
    return new Set();
  }
}

export function saveDeviceFavoriteIds(ids: Set<string>): void {
  if (typeof localStorage === "undefined") return;
  try {
    localStorage.setItem(STORAGE_KEY_DEVICE_FAVORITES, JSON.stringify([...ids]));
  } catch {
    /* quota / private mode */
  }
}
