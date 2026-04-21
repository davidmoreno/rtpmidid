import { describe, expect, it } from "vitest";
import { rtpClientConnectionSummary, rtpRemoteLabel } from "./model";

describe("rtpRemoteLabel", () => {
  it("ignores C++ sentinel hostname string null and does not produce null:0", () => {
    expect(
      rtpRemoteLabel({
        remote: { name: "", hostname: "null", port: 0 },
      }),
    ).toBe("remote");
  });

  it("formats host:port when resolved", () => {
    expect(
      rtpRemoteLabel({
        remote: { name: "Srv", hostname: "10.0.0.5", port: 5004 },
      }),
    ).toBe("Srv @ 10.0.0.5:5004");
  });
});

describe("rtpClientConnectionSummary", () => {
  it("prefers connect_hostname:connect_port from daemon status over unresolved peer.remote", () => {
    expect(
      rtpClientConnectionSummary({
        connect_hostname: "192.168.1.20",
        connect_port: "5004",
        peer: {
          remote: { name: "", hostname: "null", port: 0 },
        },
      }),
    ).toBe("192.168.1.20:5004");
  });

  it("falls back to rtpRemoteLabel when connect_* absent", () => {
    expect(
      rtpClientConnectionSummary({
        peer: {
          remote: { name: "X", hostname: "10.1.2.3", port: 5005 },
        },
      }),
    ).toBe("X @ 10.1.2.3:5005");
  });
});
