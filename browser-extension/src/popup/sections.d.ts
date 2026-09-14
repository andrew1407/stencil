// Shapes for popup/sections.js — the accordion controller plus the Alt-hover section peek.
export interface CollapsibleSections {
  isCollapsed(id: string): boolean;
  setCollapsed(id: string, want: boolean): void;
  setHook(id: string, fn: (collapsed: boolean) => void): void;
  runHook(id: string, collapsed: boolean): void;
}

export interface SectionPeek {
  enterHead(section: Element | null, altHeld: boolean): void;
  altPressed(section: Element | null): void;
  enterPeek(): void;
  leave(): void;
  altReleased(): void;
  dismiss(): void;
  sectionToggled(section: Element | null): void;
  isOpen(): boolean;
  openSection(): Element | null;
}

export declare const sections: CollapsibleSections;
export declare const sectionPeek: SectionPeek;
