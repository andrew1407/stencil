// The §2.1 and §10 op executors: the ops that act on the SESSION rather than the pixels —
// turn attachments, project save/rename/remove, and the §10 editor settings. Same facade
// paths as the toolbar; a declined confirm or an unmet precondition is a note, never a
// failed plan.
import type { OpRunner } from './opExecutors.js';

export type SettingsOpName =
  | 'image' | 'save'
  | 'theme' | 'accent' | 'lineStyle' | 'units' | 'view' | 'clear' | 'openUrl' | 'connect'
  | 'disconnect' | 'copy' | 'removeProject' | 'clearProjects' | 'compare' | 'zoom'
  | 'renameProject' | 'projectColor' | 'blankColor' | 'openProject' | 'incognito' | 'voiceChat'
  | 'chatPanel' | 'dialog' | 'clearChat';

export declare const SETTINGS_RUN: Record<SettingsOpName, OpRunner>;
