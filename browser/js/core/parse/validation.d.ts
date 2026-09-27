/** The one verdict shape. `reason` is '' when ok, otherwise the sentence to show. */
export interface Verdict { ok: boolean; reason: string; }

export function invalid(reason: string): Verdict;

/** Anything with the store's name rule on it; a null store never blocks the caller. */
export interface NameRuleHolder { validateName(name: string, exceptId?: string | null): Verdict; }
export function validateProjectName(store: NameRuleHolder | null, name: string, exceptId?: string | null): Verdict;

/** `allowEmpty` is for fields where '' is meaningful (a cleared project colour). */
export function validateHexColor(value: unknown, opts?: { allowEmpty?: boolean }): Verdict;

/** The one http(s) scheme gate every endpoint setter shares. */
export function validateHttpUrl(value: unknown): Verdict;
