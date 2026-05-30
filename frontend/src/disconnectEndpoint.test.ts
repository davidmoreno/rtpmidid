import { describe, expect, it, vi } from "vitest";
import { disconnectEndpoints } from "./disconnectEndpoint";
import type { RpcClient } from "./rpc";

function mockRpc(calls: { method: string; params: unknown }[]) {
  return {
    call: vi.fn(async (method: string, params: unknown) => {
      calls.push({ method, params });
    }),
  } as unknown as RpcClient;
}

describe("disconnectEndpoints", () => {
  it("uses router.disconnect when peer ids are known", async () => {
    const calls: { method: string; params: unknown }[] = [];
    const rpc = mockRpc(calls);
    const onStatus = vi.fn();
    const onAfter = vi.fn();

    await disconnectEndpoints(rpc, onStatus, onAfter, {
      fromIdentity: "alsa_seq:client=A,port=Out",
      toIdentity: "rtpmidi_client:hostname=h,service=S",
      fromPeerId: 1,
      toPeerId: 2,
      fromLabel: "A",
      toLabel: "B",
    });

    expect(calls.map((c) => c.method)).toEqual([
      "router.disconnect",
      "router.disconnect",
    ]);
    expect(onStatus).toHaveBeenLastCalledWith("Disconnected A ↔ B");
    expect(onAfter).toHaveBeenCalled();
  });

  it("falls back to endpoint.disconnect without peer ids", async () => {
    const calls: { method: string; params: unknown }[] = [];
    const rpc = mockRpc(calls);
    const onStatus = vi.fn();

    await disconnectEndpoints(rpc, onStatus, vi.fn(), {
      fromIdentity: "alsa_seq:client=A,port=Out",
      toIdentity: "alsa_seq:client=B,port=In",
    });

    expect(calls[0].method).toBe("endpoint.disconnect");
    expect(onStatus).toHaveBeenLastCalledWith(
      expect.stringContaining("Disconnected"),
    );
  });

  it("reports errors from the RPC", async () => {
    const rpc = {
      call: vi.fn(async () => {
        throw new Error("peer not found");
      }),
    } as unknown as RpcClient;
    const onStatus = vi.fn();

    await disconnectEndpoints(rpc, onStatus, vi.fn(), {
      fromIdentity: "a",
      toIdentity: "b",
    });

    expect(onStatus).toHaveBeenLastCalledWith(
      "Disconnect failed: Error: peer not found",
    );
  });
});
