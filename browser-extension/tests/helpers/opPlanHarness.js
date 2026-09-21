// Plan wrappers shared by the llmOp*.test.js suites: a 10-image listing (and a 5-tab one)
// unless a test narrows it, plus the registry bullet lookup.
import { OP_REGISTRY, parseOpPlan } from '../../src/llm/op/plan.js';

export const parse = (obj, listingLength = 10) =>
  parseOpPlan(typeof obj === 'string' ? obj : JSON.stringify(obj), { listingLength });

export const parseT = (obj, { listingLength = 10, tabsLength = 5 } = {}) =>
  parseOpPlan(JSON.stringify(obj), { listingLength, tabsLength });

export const plan = (actions, extra = {}) => ({ version: 1, reply: 'ok', actions, variants: [], ...extra });

export const bulletOf = (name) => OP_REGISTRY.find((e) => e.name === name).bullet;
