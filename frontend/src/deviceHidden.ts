/** Browser-local hidden devices (endpoint ids). */

export const STORAGE_KEY_DEVICE_HIDDEN = "rtpmidid-ui-device-hidden";

/** When true, manually hidden and auto-hidden export endpoints are shown in the grid. */
export const STORAGE_KEY_SHOW_HIDDEN_DEVICES = "rtpmidid-ui-show-hidden-devices";

export function loadDeviceHiddenIds(): Set<string> {
  if (typeof localStorage === "undefined") return new Set();
  try {
    const raw = localStorage.getItem(STORAGE_KEY_DEVICE_HIDDEN);
    if (!raw) return new Set();
    const parsed = JSON.parse(raw) as unknown;
    if (!Array.isArray(parsed)) return new Set();
    return new Set(parsed.filter((x): x is string => typeof x === "string"));
  } catch {
    return new Set();
  }
}

export function saveDeviceHiddenIds(ids: Set<string>): void {
  if (typeof localStorage === "undefined") return;
  try {
    localStorage.setItem(STORAGE_KEY_DEVICE_HIDDEN, JSON.stringify([...ids]));
  } catch {
    /* quota / private mode */
  }
}

export function loadShowHiddenDevices(): boolean {
  if (typeof localStorage === "undefined") return false;
  try {
    const raw = localStorage.getItem(STORAGE_KEY_SHOW_HIDDEN_DEVICES);
    if (raw === null) return false;
    return raw === "1" || raw === "true";
  } catch {
    return false;
  }
}

export function saveShowHiddenDevices(value: boolean): void {
  if (typeof localStorage === "undefined") return;
  try {
    localStorage.setItem(STORAGE_KEY_SHOW_HIDDEN_DEVICES, value ? "1" : "0");
  } catch {
    /* quota / private mode */
  }
}
