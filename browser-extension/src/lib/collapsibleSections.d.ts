export interface CollapsibleSections {
  isCollapsed(id: string): boolean;
  setCollapsed(id: string, want: boolean): void;
  setHook(id: string, fn: (collapsed: boolean) => void): void;
  runHook(id: string, collapsed: boolean): void;
}
export declare const createCollapsibleSections: (opts?: { doc?: Document;
  beforeToggle?: (section: Element | null) => void; onUserToggle?: (id: string) => void }) => CollapsibleSections;
