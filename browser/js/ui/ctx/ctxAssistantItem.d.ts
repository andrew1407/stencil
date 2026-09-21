import type { LlmSettings } from '../../llm/llmSettings.js';

/** Exists only when a provider is configured; an unreachable one still shows it. */
export declare const assistantEnabled: (settings: Pick<LlmSettings, 'provider'> | null | undefined) => boolean;

/** The Assistant submenu's markup: gating + flyout skeleton, filled in by ctxAssistant.js. */
export declare const assistantItemHtml: () => string;
