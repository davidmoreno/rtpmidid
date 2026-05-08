import type { ComponentChildren } from "preact";

type Props = {
  children: ComponentChildren;
  onClick?: () => void;
  type?: "button" | "submit";
  disabled?: boolean;
};

export function Button({ children, onClick, type = "button", disabled }: Props) {
  return (
    <button
      type={type}
      disabled={disabled}
      onClick={onClick}
      class="border-2 border-zinc-900 bg-amber-300 px-3 py-1 font-mono text-sm font-bold uppercase shadow-[3px_3px_0_0_#18181b] hover:bg-amber-200 hover:ring-2 hover:ring-inset hover:ring-zinc-900 active:translate-x-[2px] active:translate-y-[2px] active:shadow-none disabled:opacity-50 dark:border-zinc-100 dark:bg-amber-600 dark:shadow-[3px_3px_0_0_#fafafa] dark:hover:bg-amber-500 dark:hover:ring-zinc-100"
    >
      {children}
    </button>
  );
}
