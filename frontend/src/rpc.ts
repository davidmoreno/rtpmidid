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

function reconnectDelayMs(attemptIndex: number): number {
  const base = Math.min(30_000, 800 * 1.45 ** attemptIndex);
  const jitter = base * (0.15 + Math.random() * 0.35);
  return Math.round(base + jitter);
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

  private manualDisconnect = false;
  private authFatal = false;
  private reconnectAttempt = 0;
  private reconnectTimer: ReturnType<typeof setTimeout> | undefined;
  private connectionGeneration = 0;
  private sessionReady = false;
  private pendingInitialResolve = true;
  private connectResolve: (() => void) | null = null;
  private connectReject: ((e: Error) => void) | null = null;
  private onReconnectCb: (() => void) | null = null;

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

  /** Called after each successful reconnect (not after the initial connection). */
  setOnReconnect(cb: (() => void) | null) {
    this.onReconnectCb = cb;
  }

  /**
   * Resolves when the WebSocket session is ready (including optional _auth).
   * Keeps retrying with backoff until then unless disconnect() or fatal auth failure.
   */
  connect(): Promise<void> {
    this.manualDisconnect = false;
    this.authFatal = false;
    this.pendingInitialResolve = true;
    this.reconnectAttempt = 0;
    this.sessionReady = false;

    if (this.reconnectTimer !== undefined) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = undefined;
    }

    return new Promise((resolve, reject) => {
      this.connectResolve = resolve;
      this.connectReject = reject;
      this.beginConnectionAttempt();
    });
  }

  disconnect() {
    this.manualDisconnect = true;
    if (this.reconnectTimer !== undefined) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = undefined;
    }
    this.connectionGeneration++;
    this.ws?.close();
    this.ws = null;
    this.sessionReady = false;
    this.rejectAllPending(new Error("Disconnected"));
    if (this.pendingInitialResolve && this.connectReject) {
      this.pendingInitialResolve = false;
      this.connectReject(new Error("Disconnected"));
      this.connectReject = null;
      this.connectResolve = null;
    }
    this.onStatus("Disconnected.");
  }

  private rejectAllPending(err: Error) {
    for (const [, p] of this.pending) {
      p.reject(err);
    }
    this.pending.clear();
  }

  private finishSessionReady(gen: number) {
    if (gen !== this.connectionGeneration) return;
    this.sessionReady = true;
    this.reconnectAttempt = 0;
    if (this.pendingInitialResolve) {
      this.pendingInitialResolve = false;
      this.connectResolve?.();
      this.connectResolve = null;
      this.connectReject = null;
    } else {
      this.onReconnectCb?.();
    }
    this.onStatus("Connected.");
  }

  private scheduleReconnect() {
    if (this.manualDisconnect || this.authFatal) return;
    const delay = reconnectDelayMs(this.reconnectAttempt++);
    const sec = (delay / 1000).toFixed(1);
    this.onStatus(`Reconnecting in ${sec}s…`);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = undefined;
      this.beginConnectionAttempt();
    }, delay);
  }

  private beginConnectionAttempt() {
    if (this.manualDisconnect || this.authFatal) return;

    const gen = ++this.connectionGeneration;
    const needAuth = !!(this.authUser && this.authPass);
    this.authed = !needAuth;

    const url = wsUrl();
    if (this.pendingInitialResolve && this.reconnectAttempt === 0) {
      this.onStatus(`Connecting ${url}…`);
    }

    const ws = new WebSocket(url);
    this.ws = ws;

    ws.onopen = () => {
      if (gen !== this.connectionGeneration) return;
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
        this.finishSessionReady(gen);
      }
    };

    ws.onerror = () => {
      if (gen !== this.connectionGeneration) return;
      // Firefox often fires error immediately before close; do not reject here.
      if (!this.sessionReady) {
        this.onStatus("Connection error (retrying)…");
      }
    };

    ws.onclose = () => {
      if (gen !== this.connectionGeneration) return;
      this.ws = null;
      if (this.authFatal) return;

      const wasReady = this.sessionReady;
      this.sessionReady = false;

      if (this.manualDisconnect) {
        if (this.pendingInitialResolve && this.connectReject) {
          this.pendingInitialResolve = false;
          this.connectReject(new Error("Disconnected"));
          this.connectReject = null;
          this.connectResolve = null;
        }
        return;
      }

      if (!wasReady) {
        this.rejectAllPending(new Error("WebSocket not connected"));
        this.scheduleReconnect();
        return;
      }

      this.rejectAllPending(new Error("Connection lost"));
      this.onStatus("Disconnected. Reconnecting…");
      this.scheduleReconnect();
    };

    ws.onmessage = (ev) => {
      if (gen !== this.connectionGeneration) return;
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
        this.finishSessionReady(gen);
        return;
      }
      if (data.error !== undefined && !this.authed) {
        this.onStatus(`Auth failed: ${String(data.error)}`);
        this.authFatal = true;
        this.pendingInitialResolve = false;
        this.connectReject?.(new Error(String(data.error)));
        this.connectReject = null;
        this.connectResolve = null;
        ws.close();
      }
    };
  }

  async call(method: string, params: unknown = {}): Promise<unknown> {
    const ws = this.ws;
    if (
      !ws ||
      ws.readyState !== WebSocket.OPEN ||
      !this.sessionReady
    ) {
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
