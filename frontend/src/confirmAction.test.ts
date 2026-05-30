import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { runWithConfirm, skipConfirmModifier } from "./confirmAction";

describe("confirmAction", () => {
  let confirmMock: ReturnType<typeof vi.fn>;

  beforeEach(() => {
    confirmMock = vi.fn();
    vi.stubGlobal("confirm", confirmMock);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("skipConfirmModifier detects shift/ctrl/meta", () => {
    expect(
      skipConfirmModifier({ shiftKey: true, ctrlKey: false, metaKey: false }),
    ).toBe(true);
    expect(
      skipConfirmModifier({ shiftKey: false, ctrlKey: true, metaKey: false }),
    ).toBe(true);
    expect(
      skipConfirmModifier({ shiftKey: false, ctrlKey: false, metaKey: true }),
    ).toBe(true);
    expect(
      skipConfirmModifier({ shiftKey: false, ctrlKey: false, metaKey: false }),
    ).toBe(false);
  });

  it("runWithConfirm calls action when confirmed", () => {
    confirmMock.mockReturnValue(true);
    const action = vi.fn();
    runWithConfirm(
      { shiftKey: false, ctrlKey: false, metaKey: false } as MouseEvent,
      "Remove device?",
      action,
    );
    expect(confirmMock).toHaveBeenCalled();
    expect(action).toHaveBeenCalled();
  });

  it("runWithConfirm skips confirm with shift key", () => {
    const action = vi.fn();
    runWithConfirm(
      { shiftKey: true, ctrlKey: false, metaKey: false } as MouseEvent,
      "Remove device?",
      action,
    );
    expect(confirmMock).not.toHaveBeenCalled();
    expect(action).toHaveBeenCalled();
  });

  it("runWithConfirm aborts when confirm declined", () => {
    confirmMock.mockReturnValue(false);
    const action = vi.fn();
    runWithConfirm(
      { shiftKey: false, ctrlKey: false, metaKey: false } as MouseEvent,
      "Remove device?",
      action,
    );
    expect(action).not.toHaveBeenCalled();
  });
});
