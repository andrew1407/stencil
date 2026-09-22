import type { FormulaEngine } from './formulaEngine.js';
import type { FormulaContext } from './formulaContext.js';

/** The one verdict shape. `reason` is '' when ok, otherwise the sentence to show. */
export interface Verdict { ok: boolean; reason: string; }

/** The accepted verdict, frozen — pass it around, never edit it. */
export const VALID: Readonly<Verdict>;
export function invalid(reason: string): Verdict;

/** Anything with the store's name rule on it; a null store never blocks the caller. */
export interface NameRuleHolder { validateName(name: string, exceptId?: string | null): Verdict; }
export function validateProjectName(store: NameRuleHolder | null, name: string, exceptId?: string | null): Verdict;

/** `allowEmpty` is for fields where '' is meaningful (a cleared project colour). */
export function validateHexColor(value: unknown, opts?: { allowEmpty?: boolean }): Verdict;
export function validateAccent(value: unknown): Verdict;
export function validatePageSize(value: unknown): Verdict;
export function validateLengthToken(token: unknown): Verdict;
export function validateDuration(spec: unknown): Verdict;

/** The one http(s) scheme gate every endpoint setter shares. */
export const HTTP_URL_RE: RegExp;
export function validateHttpUrl(value: unknown): Verdict;

export function validateHotkey(combo: unknown): Verdict;

/** The engine comes in because it is wasm-bound and the caller already holds the app's. */
export function validateFormula(engine: FormulaEngine | null,
  expr: unknown, axis?: 'x' | 'y', ctx?: FormulaContext | null): Verdict;

/** An accepted file carries the parsed project alongside the verdict. */
export function validateProjectFileText(text: string): Verdict & { project?: Record<string, unknown> };
