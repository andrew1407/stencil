// Shapes for popup/editorDialogs.js — the two confirm dialogs editor mode raises through
// popup/dialogShell.js.
export declare const confirmDialog: (
  titleText: string, subject: string, warning: string, confirmLabel: string, anchor?: Element | null,
) => Promise<boolean>;
/** Resolves the chosen ImportMode, or undefined on Cancel/click-away/Escape. */
export declare const promptImportMode: (
  state: { projectName?: string; imageName?: string; incognito?: boolean }, anchor?: Element | null,
) => Promise<'new' | 'replace' | 'replace-keep' | undefined>;
