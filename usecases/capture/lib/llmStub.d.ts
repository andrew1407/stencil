export interface LlmStub { url: string; queue(plan: object): void; close(): Promise<void> }

export function startLlmStub(): Promise<LlmStub>;
export function chatOnlyPlan(reply: string): { version: number; reply: string; actions: []; variants: [] };
