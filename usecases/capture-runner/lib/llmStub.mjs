// The canned assistant: e2e's LLM stub, fed the op-plans config/shared.json declares. Every
// plan is a valid contract §1 envelope, so the app really executes it on the canvas.
import { e2e } from './playwright.mjs';

export const startLlmStub = async () => (await e2e('helpers/llm-stub.js')).startLlmStub();

export const chatOnlyPlan = (reply) => ({ version: 1, reply, actions: [], variants: [] });
