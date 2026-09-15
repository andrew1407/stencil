// How ONE vocabulary entry reads, whatever table it came from: a heading over four optional parts.
'use strict';

/* `**@crop** — Cut the picture down…` over a fenced signature, the prose, then a fenced
 * example. Every section is optional; `fence` is the language the two blocks are lit in. */
const markdownFor = (label, entry, fence = 'stc') => {
  if (!entry) return '';
  const open = `\`\`\`${fence}`;
  const parts = [`**${label}** — ${entry.summary}`];
  if (entry.signature) parts.push([open, entry.signature, '```'].join('\n'));
  if (entry.detail) parts.push(entry.detail);
  if (entry.example) parts.push([open, entry.example, '```'].join('\n'));
  return parts.join('\n\n');
};

/* One table's `explain`: `normalize` spells the word the way the table keys it, `label` is the
 * heading, `decorate` adds what only that table has to say, and an unknown word gives ''. */
const makeExplain = ({ normalize = String, lookup, label, fence, decorate }) => (word) => {
  const key = normalize(word);
  const entry = lookup(key);
  if (!entry) return '';
  const markdown = markdownFor(label(key, entry), entry, fence);
  return decorate ? decorate(markdown, entry) : markdown;
};

module.exports = { makeExplain, markdownFor };
