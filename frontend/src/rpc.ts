/** JSON-RPC over WebSocket (same wire format as rtpmidid-cli). */

export type JsonRpcResponse = {
  id?: number | string;
  result?: unknown;
  error?: unknown;
  event?: string;
  params?: unknown;
};

export type RpcConnectionPhase =
  | "connecting"
  | "authenticating"
  | "connected"
  | "waiting_retry"
  | "disconnected"
  | "auth_failed";

export type RpcConnectionState = {
  phase: RpcConnectionPhase;
  url: string;
  /** 1-based attempt number for the next connection try. */
  attempt: number;
  /** Milliseconds until the next automatic retry (while waiting_retry). */
  retryInMs?: number;
  detail?: string;
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

export function formatRetryCountdown(ms: number): string {
  if (ms <= 0) return "now";
  if (ms < 1000) return `${(ms / 1000).toFixed(1)} s`;
  return `${(ms / 1000).toFixed(1)} s`;
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
  private countdownTimer: ReturnType<typeof setInterval> | undefined;
  private retryScheduledAt = 0;
  private retryDelayMs = 0;
  private connectionGeneration = 0;
  private sessionReady = false;
  private pendingInitialResolve = true;
  private connectResolve: (() => void) | null = null;
  private connectReject: ((e: Error) => void) | null = null;
  private onReconnectCb: (() => void) | null = null;
  private onConnectionStateChange: ((state: RpcConnectionState) => void) | null =
    null;

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

  setOnConnectionStateChange(
    h: ((state: RpcConnectionState) => void) | null,
  ) {
    this.onConnectionStateChange = h;
  }

  /** Called after each successful reconnect (not after the initial connection). */
  setOnReconnect(cb: (() => void) | null) {
    this.onReconnectCb = cb;
  }

  isConnected(): boolean {
    return this.sessionReady;
  }

  /**
   * Drop backoff, close any in-flight socket, and connect immediately.
   * Safe on page reload / bfcache restore when the session is not ready.
   */
  forceReconnect() {
    if (this.manualDisconnect || this.authFatal) return;
    this.clearRetryTimers();
    this.reconnectAttempt = 0;
    if (this.sessionReady) return;
    this.connectionGeneration++;
    this.ws?.close();
    this.ws = null;
    this.beginConnectionAttempt();
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

    this.clearRetryTimers();

    return new Promise((resolve, reject) => {
      this.connectResolve = resolve;
      this.connectReject = reject;
      this.beginConnectionAttempt();
    });
  }

  disconnect() {
    this.manualDisconnect = true;
    this.clearRetryTimers();
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
    this.emitConnectionState({ phase: "disconnected", detail: "Disconnected." });
    this.onStatus("Disconnected.");
  }

  private clearCountdownTimer() {
    if (this.countdownTimer !== undefined) {
      clearInterval(this.countdownTimer);
      this.countdownTimer = undefined;
    }
  }

  private clearRetryTimers() {
    if (this.reconnectTimer !== undefined) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = undefined;
    }
    this.clearCountdownTimer();
    this.retryScheduledAt = 0;
    this.retryDelayMs = 0;
  }

  private rejectAllPending(err: Error) {
    for (const [, p] of this.pending) {
      p.reject(err);
    }
    this.pending.clear();
  }

  private emitConnectionState(partial: Partial<RpcConnectionState> & Pick<RpcConnectionState, "phase">) {
    const state: RpcConnectionState = {
      url: wsUrl(),
      attempt: this.reconnectAttempt + 1,
      ...partial,
    };
    if (state.phase === "waiting_retry" && this.retryScheduledAt > 0) {
      state.retryInMs = Math.max(
        0,
        this.retryScheduledAt + this.retryDelayMs - Date.now(),
      );
    }
    this.onConnectionStateChange?.(state);
  }

  private finishSessionReady(gen: number) {
    if (gen !== this.connectionGeneration) return;
    this.sessionReady = true;
    this.reconnectAttempt = 0;
    this.clearRetryTimers();
    if (this.pendingInitialResolve) {
      this.pendingInitialResolve = false;
      this.connectResolve?.();
      this.connectResolve = null;
      this.connectReject = null;
    } else {
      this.onReconnectCb?.();
    }
    this.emitConnectionState({ phase: "connected", detail: "Connected." });
    this.onStatus("Connected.");
  }

  private startRetryCountdown() {
    this.clearCountdownTimer();
    const tick = () => {
      const remaining = Math.max(
        0,
        this.retryScheduledAt + this.retryDelayMs - Date.now(),
      );
      this.emitConnectionState({
        phase: "waiting_retry",
        retryInMs: remaining,
        detail: `Next retry in ${formatRetryCountdown(remaining)} (attempt ${this.reconnectAttempt + 1})`,
      });
    };
    tick();
    this.countdownTimer = setInterval(tick, 200);
  }

  private scheduleReconnect() {
    if (this.manualDisconnect || this.authFatal) return;
    const delay = reconnectDelayMs(this.reconnectAttempt++);
    this.retryDelayMs = delay;
    this.retryScheduledAt = Date.now();
    this.startRetryCountdown();
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = undefined;
      this.clearCountdownTimer();
      this.beginConnectionAttempt();
    }, delay);
  }

  private beginConnectionAttempt() {
    if (this.manualDisconnect || this.authFatal) return;

    const gen = ++this.connectionGeneration;
    const needAuth = !!(this.authUser && this.authPass);
    this.authed = !needAuth;

    const url = wsUrl();
    this.emitConnectionState({
      phase: "connecting",
      detail: `Connecting to ${url}…`,
    });

    const ws = new WebSocket(url);
    this.ws = ws;

    ws.onopen = () => {
      if (gen !== this.connectionGeneration) return;
      if (needAuth) {
        this.emitConnectionState({
          phase: "authenticating",
          detail: "WebSocket open — sending credentials…",
        });
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
      if (!this.sessionReady) {
        this.emitConnectionState({
          phase: "connecting",
          detail: "Connection error — waiting to retry…",
        });
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
      this.emitConnectionState({
        phase: "waiting_retry",
        detail: "Connection lost — scheduling retry…",
      });
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
        const err = String(data.error);
        this.emitConnectionState({
          phase: "auth_failed",
          detail: `Auth failed: ${err}`,
        });
        this.onStatus(`Auth failed: ${err}`);
        this.authFatal = true;
        this.pendingInitialResolve = false;
        this.connectReject?.(new Error(err));
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

  /** Subscribe to one or more event channels. */
  async subscribe(channels: string[]): Promise<void> {
    await this.call("subscribe", { channels });
  }

  /** Unsubscribe from one or more event channels. */
  async unsubscribe(channels: string[]): Promise<void> {
    await this.call("unsubscribe", { channels });
  }
}
