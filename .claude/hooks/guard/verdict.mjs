// The guard's verdicts: every rule module returns one of these, and the entry turns `ask` and
// `deny` into a PreToolUse permission decision.

export const allow = () => ({ decision: 'allow' });
export const ask = (reason) => ({ decision: 'ask', reason });
export const deny = (reason) => ({ decision: 'deny', reason });
