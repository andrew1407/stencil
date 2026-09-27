// The plan-level half of the mechanical op-plan cases: the envelope caps (actions, variants,
// actions inside a variant) and the §11 ask card, derived from opRegistry.json for every profile.

export const cardCases = (registry, limit, add) => {
  const ALL = Object.keys(registry.profiles);
  const plan = (actions) => ({ version: 1, reply: 'ok', actions });
  // ── plan envelope (every profile) ─────────────────────────────────────────
  const rot = () => ({ op: 'rotate', dir: 'left' });
  const A = limit('MAX_ACTIONS'), V = limit('MAX_VARIANTS');
  add({ name: 'gen-envelope-actions-max-ok', generated: true, profiles: ['all'], input: plan(Array.from({ length: A }, rot)), expect: 'valid', reason: `${A} actions is the cap` });
  add({ name: 'gen-envelope-actions-over', generated: true, profiles: ['all'], input: plan(Array.from({ length: A + 1 }, rot)), expect: 'invalid', reason: `more than ${A} actions — the length check fires before per-op validation on every surface` });
  add({ name: 'gen-envelope-variants-max-ok', generated: true, profiles: ['all'], input: { ...plan([]), variants: Array.from({ length: V }, (_, i) => ({ label: `v${i + 1}`, actions: [rot()] })) }, expect: 'valid', reason: `${V} variants is the cap (the extension drops variants with a warning — still valid)` });
  // The extension never walks variants (it drops them with a warning), so the variant caps
  // are editor/console/bot/mcp cases.
  const VARIANT_PROFILES = ALL.filter((p) => p !== 'extension');
  add({ name: 'gen-envelope-variants-over', generated: true, profiles: VARIANT_PROFILES, input: { ...plan([]), variants: Array.from({ length: V + 1 }, (_, i) => ({ label: `v${i + 1}`, actions: [rot()] })) }, expect: 'invalid', reason: `more than ${V} variants — checked before the variants are walked on every surface` });
  add({ name: 'gen-envelope-variant-actions-over', generated: true, profiles: VARIANT_PROFILES, input: { ...plan([]), variants: [{ label: 'v', actions: Array.from({ length: A + 1 }, rot) }] }, expect: 'invalid', reason: `more than ${A} actions inside a variant` });

  // ── §11 ask card (every profile) ──────────────────────────────────────────
  const ask = registry.ask.schema;
  const askBase = () => ({ question: 'Q', options: [{ label: 'A' }, { label: 'B' }] });
  const askPlan = (card) => ({ version: 1, reply: 'ok', ask: card });
  const askInvalid = (slug, card, reason) => add({ name: `gen-ask-${slug}`, generated: true, profiles: ['all'], input: askPlan(card), expect: 'invalid', reason });
  const askValid = (slug, card, reason) => add({ name: `gen-ask-${slug}`, generated: true, profiles: ['all'], input: askPlan(card), expect: 'valid', reason });
  askValid('minimal', askBase(), 'the smallest card the registry admits');
  askInvalid('unknown-field', { ...askBase(), zzzCanary: 1 }, 'an undeclared card field fails the plan');
  for (const [k, s] of Object.entries(ask.keys)) {
    if (s.required) { const c = askBase(); delete c[k]; askInvalid(`${k}-missing`, c, `"ask.${k}" is required`); }
    askInvalid(`${k}-wrong-type`, { ...askBase(), [k]: s.type === 'object' ? 5 : {} }, `"ask.${k}" must be a ${s.type}`);
    if (s.enum) askInvalid(`${k}-enum-bogus`, { ...askBase(), [k]: 'zzz' }, `"ask.${k}" must be one of ${s.enum.join(', ')}`);
    if (s.nonEmpty) askInvalid(`${k}-blank`, { ...askBase(), [k]: '   ' }, `"ask.${k}" must be a non-empty string`);
    if (s.type === 'string' && s.maxChars != null) {
      const n = limit(s.maxChars);
      askValid(`${k}-maxchars-ok`, { ...askBase(), [k]: 'x'.repeat(n) }, `"ask.${k}" at its ${n}-character cap`);
      askInvalid(`${k}-maxchars-over`, { ...askBase(), [k]: 'x'.repeat(n + 1) }, `"ask.${k}" is longer than ${n} characters`);
    }
    if (s.type === 'array') {
      const min = limit(s.minItems), max = limit(s.maxItems);
      const opt = (i) => ({ label: `O${i + 1}` });
      askInvalid(`${k}-below-min-items`, { ...askBase(), [k]: Array.from({ length: min - 1 }, (_, i) => opt(i)) }, `"ask.${k}" must hold ${min}..${max} entries`);
      askValid(`${k}-max-items-ok`, { ...askBase(), [k]: Array.from({ length: max }, (_, i) => opt(i)) }, `"ask.${k}" at its ${max}-entry cap`);
      askInvalid(`${k}-above-max-items`, { ...askBase(), [k]: Array.from({ length: max + 1 }, (_, i) => opt(i)) }, `"ask.${k}" holds more than ${max} entries`);
    }
  }
  const optFields = ask.keys.options.items.fields;
  const withOpt = (o) => ({ ...askBase(), options: [{ label: 'A', ...o }, { label: 'B' }] });
  askInvalid('option-unknown-field', withOpt({ zzzCanary: 1 }), 'an undeclared option field fails the plan');
  askInvalid('option-label-blank', withOpt({ label: '  ' }), 'an option label must be non-empty');
  askInvalid('option-label-over', withOpt({ label: 'x'.repeat(limit(optFields.label.maxChars) + 1) }), `an option label is capped at ${limit(optFields.label.maxChars)} characters`);
  askValid('option-label-max-ok', withOpt({ label: 'x'.repeat(limit(optFields.label.maxChars)) }), 'an option label at its cap');
  askInvalid('option-actions-and-image', withOpt({ actions: [], image: { scanIndex: 0 } }), 'an option previews a render OR names an image — never both');
  askInvalid('option-image-no-ref', withOpt({ image: {} }), 'an image reference needs exactly one of url / projectId / scanIndex');
  askInvalid('option-image-two-refs', withOpt({ image: { url: 'https://example.com/a.png', projectId: 'p' } }), 'an image reference needs exactly one of url / projectId / scanIndex');
  askInvalid('option-image-unknown-field', withOpt({ image: { zzz: 1 } }), 'an undeclared image-reference field fails the plan');
  askInvalid('option-image-url-not-http', withOpt({ image: { url: 'data:image/png;base64,AA' } }), 'only http(s) image urls are accepted');
  askInvalid('option-image-scanindex-negative', withOpt({ image: { scanIndex: -1 } }), 'scanIndex must be an integer >= 0');

};
