import { describe, expect, it } from "vitest";
import { formatRetryCountdown } from "./rpc";

describe("formatRetryCountdown", () => {
  it("formats sub-second and second delays", () => {
    expect(formatRetryCountdown(0)).toBe("now");
    expect(formatRetryCountdown(450)).toBe("0.5 s");
    expect(formatRetryCountdown(3200)).toBe("3.2 s");
  });
});
