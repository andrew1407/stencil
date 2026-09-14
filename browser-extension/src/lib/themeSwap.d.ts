// Classic script (see accent.js): no ES exports — publishes window.StencilKit.swap.

export interface ThemeSwapKit {
  swap(fn: () => void, originId?: string | Element): void;
}
