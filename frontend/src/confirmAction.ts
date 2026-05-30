/** Shift/Ctrl/Meta+click skips the confirmation dialog. */
export function skipConfirmModifier(ev: {
  shiftKey: boolean;
  ctrlKey: boolean;
  metaKey: boolean;
}): boolean {
  return ev.shiftKey || ev.ctrlKey || ev.metaKey;
}

export const CONFIRM_SKIP_HINT = "Shift+click or Ctrl+click to skip confirmation.";

/**
 * Run @a action after optional `window.confirm`, unless modifier bypass is held.
 */
export function runWithConfirm(
  ev: MouseEvent,
  message: string,
  action: () => void | Promise<void>,
): void {
  if (!skipConfirmModifier(ev) && !globalThis.confirm(message)) return;
  void action();
}
