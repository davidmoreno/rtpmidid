import type { ComponentChildren } from "preact";

type Props = { title: string; children: ComponentChildren; class?: string };

export function Card({ title, children, class: cls = "" }: Props) {
  return (
    <section
      class={`border-2 border-zinc-900 bg-white shadow-[6px_6px_0_0_#18181b] dark:border-zinc-100 dark:bg-zinc-900 dark:shadow-[6px_6px_0_0_#fafafa] ${cls}`}
    >
      <h2 class="border-b-2 border-zinc-900 bg-zinc-200 px-3 py-2 font-mono text-sm font-bold uppercase tracking-wide dark:border-zinc-100 dark:bg-zinc-800">
        {title}
      </h2>
      <div class="p-3">{children}</div>
    </section>
  );
}
