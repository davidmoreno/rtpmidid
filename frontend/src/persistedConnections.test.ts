import { describe, expect, it } from "vitest";
import { parseConnectionsListResult } from "./persistedConnections";

describe("parseConnectionsListResult", () => {
  it("parses direction and enabled", () => {
    const parsed = parseConnectionsListResult({
      enabled: 1,
      connections: [
        {
          side_a: "rtpmidi_server:name=Peak",
          side_b: "rawmidi:device=/dev/snd/midiC0D0",
          direction: "a2b",
          enabled: 0,
          active_a: 1,
          peer_a: 3,
        },
      ],
    });
    expect(parsed.enabled).toBe(true);
    expect(parsed.connections[0].direction).toBe("a2b");
    expect(parsed.connections[0].enabled).toBe(false);
    expect(parsed.connections[0].peer_a).toBe(3);
  });
});
