import { useCallback, useEffect, useRef, useState } from "preact/hooks";
import type { RpcClient } from "../rpc";

// ── Types ───────────────────────────────────────────────────────────────────

interface LogEntry {
  seq: number;
  timestamp_us: number;
  level: number; // 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR
  file: string;
  line: number;
  message: string;
  tags: Record<string, string>;
}

interface LogQueryResult {
  entries: LogEntry[];
  total_matches: number;
  buffer_capacity: number;
  oldest_seq: number;
  newest_seq: number;
}

// ── Constants ───────────────────────────────────────────────────────────────

const LEVEL_LABELS: Record<number, string> = {
  0: "DEBUG",
  1: "INFO",
  2: "WARN",
  3: "ERROR",
};

const LEVEL_COLORS: Record<number, string> = {
  0: "ui-log-level-debug",
  1: "ui-log-level-info",
  2: "ui-log-level-warn",
  3: "ui-log-level-error",
};

const TAG_COLORS: Record<string, string> = {
  peer_id: "ui-log-tag-peer",
  component: "ui-log-tag-component",
  connection_id: "ui-log-tag-connection",
  session_id: "ui-log-tag-session",
  level: "ui-log-tag-level",
};

const POLL_INTERVAL_MS = 2000;

// ── Helpers ─────────────────────────────────────────────────────────────────

function formatTime(us: number): string {
  const d = new Date();
  // timestamp_us is relative to steady_clock epoch — show as relative or HH:MM:SS.mmm
  // For now, show seq-based ordering; absolute time would need a base offset
  return "";
}

function formatTimestamp(seq: number): string {
  // Simple: just show the time the entry was received (browser local time)
  const now = new Date();
  const hh = String(now.getHours()).padStart(2, "0");
  const mm = String(now.getMinutes()).padStart(2, "0");
  const ss = String(now.getSeconds()).padStart(2, "0");
  const ms = String(now.getMilliseconds()).padStart(3, "0");
  return `${hh}:${mm}:${ss}.${ms}`;
}

/** Build a LogQL query string from active tag filters and text search. */
function buildLogQL(
  tags: Record<string, string>,
  textFilter: string,
  negateText: boolean,
): string {
  const parts: string[] = [];

  // Stream selector
  const tagEntries = Object.entries(tags).filter(([, v]) => v);
  if (tagEntries.length > 0) {
    const pairs = tagEntries.map(([k, v]) => `${k}="${v}"`).join(", ");
    parts.push(`{${pairs}}`);
  }

  // Text filter
  if (textFilter.trim()) {
    const op = negateText ? "!=" : "|=";
    parts.push(`${op} "${textFilter.trim()}"`);
  }

  return parts.join(" ");
}

// ── Component ───────────────────────────────────────────────────────────────

type Props = {
  rpc: RpcClient;
  onStatus: (msg: string) => void;
};

export function LogsTab({ rpc, onStatus }: Props) {
  const [entries, setEntries] = useState<LogEntry[]>([]);
  const [totalMatches, setTotalMatches] = useState(0);
  const [bufferCapacity, setBufferCapacity] = useState(0);
  const [paused, setPaused] = useState(false);
  const [autoScroll, setAutoScroll] = useState(true);

  // Filters
  const [tagFilters, setTagFilters] = useState<Record<string, string>>({});
  const [textFilter, setTextFilter] = useState("");
  const [negateText, setNegateText] = useState(false);
  const [levelFilter, setLevelFilter] = useState<Set<number>>(
    () => new Set([0, 1, 2, 3]),
  );

  const lastSeqRef = useRef(0);
  const containerRef = useRef<HTMLDivElement>(null);
  const pollTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const isAtBottomRef = useRef(true);

  // ── Poll ──────────────────────────────────────────────────────────────

  const fetchLogs = useCallback(
    async (sinceSeq?: number) => {
      try {
        const q = buildLogQL(tagFilters, textFilter, negateText);
        const params: Record<string, unknown> = {};
        if (q) params.q = q;
        if (sinceSeq !== undefined) params.since_seq = sinceSeq;
        params.limit = 200;

        const result = (await rpc.call("log.query", params)) as LogQueryResult;
        if (!result || !Array.isArray(result.entries)) return;

        if (sinceSeq !== undefined && sinceSeq > 0) {
          // Append new entries
          setEntries((prev) => {
            const existing = new Set(prev.map((e) => e.seq));
            const added = result.entries.filter((e) => !existing.has(e.seq));
            return [...prev, ...added].slice(-1000);
          });
        } else {
          // Replace
          setEntries(result.entries);
        }

        setTotalMatches(result.total_matches ?? 0);
        setBufferCapacity(result.buffer_capacity ?? 0);

        if (result.newest_seq > 0) {
          lastSeqRef.current = result.newest_seq;
        }
      } catch (e) {
        onStatus(String(e));
      }
    },
    [rpc, onStatus, tagFilters, textFilter, negateText],
  );

  // Initial load + poll
  useEffect(() => {
    void fetchLogs();

    pollTimerRef.current = setInterval(() => {
      if (!paused) {
        void fetchLogs(lastSeqRef.current);
      }
    }, POLL_INTERVAL_MS);

    return () => {
      if (pollTimerRef.current) clearInterval(pollTimerRef.current);
    };
  }, [fetchLogs, paused]);

  // Refetch when filters change
  useEffect(() => {
    lastSeqRef.current = 0;
    void fetchLogs();
  }, [tagFilters, textFilter, negateText]);

  // ── Scroll ────────────────────────────────────────────────────────────

  const onScroll = useCallback(() => {
    const el = containerRef.current;
    if (!el) return;
    const atBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 24;
    isAtBottomRef.current = atBottom;
    setAutoScroll(atBottom);
  }, []);

  useEffect(() => {
    if (autoScroll && containerRef.current) {
      containerRef.current.scrollTop = containerRef.current.scrollHeight;
    }
  }, [entries, autoScroll]);

  // ── Tag chip handlers ─────────────────────────────────────────────────

  const addTagFilter = useCallback((key: string, value: string) => {
    setTagFilters((prev) => {
      if (prev[key] === value) return prev; // already set
      return { ...prev, [key]: value };
    });
    setAutoScroll(true);
  }, []);

  const removeTagFilter = useCallback((key: string) => {
    setTagFilters((prev) => {
      const next = { ...prev };
      delete next[key];
      return next;
    });
    setAutoScroll(true);
  }, []);

  const toggleLevel = useCallback((lvl: number) => {
    setLevelFilter((prev) => {
      const next = new Set(prev);
      if (next.has(lvl)) next.delete(lvl);
      else next.add(lvl);
      return next;
    });
    setAutoScroll(true);
  }, []);

  // Filter entries by level
  const displayEntries = entries.filter((e) => levelFilter.has(e.level));

  return (
    <div class="space-y-3">
      {/* Toolbar */}
      <div class="ui-peer-card-shell">
        <div class="ui-devices-toolbar">
          <div class="flex flex-wrap items-center gap-2">
            {/* Level toggles */}
            {[3, 2, 1, 0].map((lvl) => (
              <button
                key={lvl}
                type="button"
                class={`ui-filter-chip ${
                  levelFilter.has(lvl)
                    ? `${LEVEL_COLORS[lvl]} ui-filter-chip--on`
                    : "ui-filter-chip--off"
                }`}
                onClick={() => toggleLevel(lvl)}
              >
                {LEVEL_LABELS[lvl]}
              </button>
            ))}

            {/* Active tag chips */}
            {Object.entries(tagFilters).map(([key, value]) => (
              <span
                key={key}
                class={`ui-log-tag-chip ${TAG_COLORS[key] ?? "ui-log-tag-default"}`}
                title={`Filter: ${key}=${value} (click to remove)`}
                onClick={() => removeTagFilter(key)}
              >
                {key}={value} ✕
              </span>
            ))}

            {/* Text filter */}
            <div class="ui-search-wrap ml-auto">
              <input
                class="ui-input ui-search-input font-mono text-[11px]"
                value={textFilter}
                onInput={(e) => setTextFilter((e.target as HTMLInputElement).value)}
                placeholder='|= "search text"'
                aria-label="Search log text"
              />
              <button
                type="button"
                class={`ui-search-clear ${textFilter.trim() ? "" : "ui-search-clear--muted"}`}
                aria-label="Clear search"
                onClick={() => setTextFilter("")}
                disabled={!textFilter.trim()}
              >
                ×
              </button>
            </div>
            <button
              type="button"
              class={`ui-filter-chip ${
                negateText ? "ui-log-level-error ui-filter-chip--on" : "ui-filter-chip--off"
              } font-mono text-[10px]`}
              onClick={() => {
                setNegateText((v) => !v);
                setAutoScroll(true);
              }}
            >
              !=
            </button>

            {/* Controls */}
            <button
              type="button"
              class={`ui-filter-chip font-mono text-[10px] ${
                paused ? "ui-log-level-warn ui-filter-chip--on" : "ui-filter-chip--off"
              }`}
              onClick={() => setPaused((v) => !v)}
            >
              {paused ? "▶ Resume" : "⏸ Pause"}
            </button>
            <button
              type="button"
              class="ui-filter-chip ui-filter-chip--off font-mono text-[10px]"
              onClick={() => {
                setTagFilters({});
                setTextFilter("");
                setNegateText(false);
                setLevelFilter(new Set([0, 1, 2, 3]));
                setAutoScroll(true);
              }}
            >
              Clear
            </button>
          </div>
        </div>
      </div>

      {/* Stats */}
      <div class="font-mono text-[10px] ui-text-muted">
        <span class="font-black ui-text">{displayEntries.length}</span>
        {totalMatches > 0 && totalMatches !== displayEntries.length && (
          <>
            <span class="ui-text-subtle">/</span>
            <span class="font-black ui-text">{totalMatches}</span>
          </>
        )}
        <span class="ui-text-subtle">/{bufferCapacity || "—"} entries</span>
        {paused && (
          <span class="ml-2 ui-text-subtle">(paused)</span>
        )}
      </div>

      {/* Log entries */}
      <div
        ref={containerRef}
        onScroll={onScroll}
        class="max-h-[min(70vh,36rem)] overflow-y-auto rounded-[var(--radius-md)] border border-[color:var(--color-border)] bg-[color:var(--color-surface)] font-mono text-[11px] leading-relaxed"
      >
        {displayEntries.length === 0 ? (
          <div class="p-4 text-center ui-text-subtle">
            {entries.length === 0
              ? "No log entries. Logs will appear here as the daemon runs."
              : "No entries match the current filters."}
          </div>
        ) : (
          displayEntries.map((entry) => (
            <LogRow
              key={entry.seq}
              entry={entry}
              onTagClick={addTagFilter}
            />
          ))
        )}
      </div>
    </div>
  );
}

// ── Log Row Component ───────────────────────────────────────────────────────

function LogRow({
  entry,
  onTagClick,
}: {
  entry: LogEntry;
  onTagClick: (key: string, value: string) => void;
}) {
  const levelClass = LEVEL_COLORS[entry.level] ?? "";
  const levelLabel = LEVEL_LABELS[entry.level] ?? "?";

  const tagEntries = Object.entries(entry.tags).filter(
    ([, v]) => v && v !== "",
  );

  return (
    <div
      class={`flex gap-2 border-b border-[color:var(--color-border-muted)] px-3 py-1 hover:bg-[color:var(--color-surface-2)] ${levelClass}`}
    >
      {/* Timestamp + level */}
      <div class="shrink-0 text-right tabular-nums">
        <span class="ui-text-subtle">{formatTimestamp(entry.seq)}</span>
        <span class={`ml-2 font-black uppercase ${levelClass}`}>
          {levelLabel}
        </span>
      </div>

      {/* Source */}
      <span class="shrink-0 ui-text-subtle">
        {entry.file}:{entry.line}
      </span>

      {/* Tag chips */}
      {tagEntries.length > 0 && (
        <span class="flex shrink-0 flex-wrap gap-1">
          {tagEntries.map(([key, value]) => (
            <span
              key={key}
              class={`inline-flex cursor-pointer items-center rounded-[var(--radius-sm)] px-1 py-0 text-[10px] font-black uppercase ${
                TAG_COLORS[key] ?? "ui-log-tag-default"
              }`}
              title={`Filter: ${key}="${value}"`}
              onClick={() => onTagClick(key, value)}
            >
              {key}={value}
            </span>
          ))}
        </span>
      )}

      {/* Message */}
      <span class="min-w-0 flex-1 truncate">{entry.message}</span>
    </div>
  );
}
