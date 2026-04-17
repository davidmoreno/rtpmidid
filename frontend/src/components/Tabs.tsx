import type { ComponentChildren } from "preact";

export type TabDef = { id: string; label: string; content: ComponentChildren };

type Props = {
  tabs: TabDef[];
  active: string;
  onChange: (id: string) => void;
};

export function Tabs({ tabs, active, onChange }: Props) {
  return (
    <div>
      <div class="mb-4 flex flex-wrap gap-1 border-b-2 border-zinc-900 dark:border-zinc-100">
        {tabs.map((t) => (
          <button
            type="button"
            key={t.id}
            class={
              t.id === active
                ? "border-2 border-b-0 border-zinc-900 bg-white px-4 py-2 font-mono text-sm font-bold dark:border-zinc-100 dark:bg-zinc-900"
                : "border-2 border-transparent px-4 py-2 font-mono text-sm text-zinc-600 hover:bg-zinc-200 dark:text-zinc-400 dark:hover:bg-zinc-800"
            }
            onClick={() => onChange(t.id)}
          >
            {t.label}
          </button>
        ))}
      </div>
      {tabs.find((x) => x.id === active)?.content}
    </div>
  );
}
