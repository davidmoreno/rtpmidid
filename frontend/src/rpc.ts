/** JSON-RPC over WebSocket (same wire format as rtpmidid-cli). */

export type JsonRpcResponse = {
  id?: number | string;
  result?: unknown;
  error?: unknown;
  event?: string;
};

function wsUrl(): string {
  const { protocol, host } = window.location;
  const wsProto = protocol === "https:" ? "wss:" : "ws:";
  return `${wsProto}//${host}/ws`;
}

export class RpcClient {
  private ws: WebSocket | null = null;
  private nextId = 1;
  private pending = new Map<
    number,
    { resolve: (v: unknown) => void; reject: (e: Error) => void }
  >();
  private eventHandler: ((ev: JsonRpcResponse) => void) | null = null;
  private authed = false;
  private authUser = "";
  private authPass = "";

  constructor(
    private readonly onStatus: (msg: string) => void,
    private readonly onEvent?: (ev: JsonRpcResponse) => void,
  ) {}

  setAuth(user: string, pass: string) {
    this.authUser = user;
    this.authPass = pass;
  }

  setEventHandler(h: ((ev: JsonRpcResponse) => void) | null) {
    this.eventHandler = h;
  }

  connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      const needAuth = !!(this.authUser && this.authPass);
      this.authed = !needAuth;
      const url = wsUrl();
      this.onStatus(`Connecting ${url}…`);
      const ws = new WebSocket(url);
      this.ws = ws;

      let settled = false;
      const finishOk = () => {
        if (!settled) {
          settled = true;
          this.onStatus("Connected.");
          resolve();
        }
      };

      ws.onopen = () => {
        if (needAuth) {
          const id = this.nextId++;
          ws.send(
            JSON.stringify({
              method: "_auth",
              params: { username: this.authUser, password: this.authPass },
              id,
            }),
          );
        } else {
          finishOk();
        }
      };
      ws.onerror = () => {
        if (!settled) {
          settled = true;
          reject(new Error("WebSocket error"));
        }
      };
      ws.onclose = () => {
        this.ws = null;
        this.onStatus("Disconnected.");
      };
      ws.onmessage = (ev) => {
        let data: JsonRpcResponse;
        try {
          data = JSON.parse(String(ev.data)) as JsonRpcResponse;
        } catch {
          return;
        }
        if (data.event) {
          this.eventHandler?.(data);
          this.onEvent?.(data);
          return;
        }
        if (data.id !== undefined && data.id !== null) {
          const id = Number(data.id);
          const p = this.pending.get(id);
          if (p) {
            this.pending.delete(id);
            if (data.error !== undefined) {
              p.reject(new Error(String(data.error)));
            } else {
              p.resolve(data.result);
            }
            return;
          }
        }
        if (needAuth && !this.authed && data.result === "ok") {
          this.authed = true;
          finishOk();
          return;
        }
        if (data.error !== undefined && !this.authed) {
          this.onStatus(`Auth failed: ${String(data.error)}`);
          if (!settled) {
            settled = true;
            reject(new Error(String(data.error)));
          }
        }
      };
    });
  }

  disconnect() {
    this.ws?.close();
    this.ws = null;
  }

  async call(method: string, params: unknown = {}): Promise<unknown> {
    const ws = this.ws;
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      throw new Error("WebSocket not connected");
    }
    const id = this.nextId++;
    const body = JSON.stringify({ method, params, id });
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      ws.send(body);
    });
  }
}
