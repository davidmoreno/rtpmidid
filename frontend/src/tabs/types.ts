export type StatusResult = {
  version?: string;
  router?: unknown[];
  mdns?: Record<string, unknown>;
  settings?: Record<string, unknown>;
  web?: { url: string; accessible: boolean };
};
