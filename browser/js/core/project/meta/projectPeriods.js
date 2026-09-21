// Project expiry: the refresh presets, the arithmetic over them and the expired /
// expiring-soon predicates. Twin of core/state/ProjectsStore.cpp's period helpers.
export const EXPIRY_MS = 7 * 24 * 60 * 60 * 1000; // one week (also the "week" preset)
export const WARN_MS = 24 * 60 * 60 * 1000; // warn once a project is within a day of expiry

// Fixed durations (month=30d, year=365d) so this and core/state/ProjectsStore.cpp
// (ProjectsStore::periodMs) stay identical with no calendar library.
const DAY_MS = 24 * 60 * 60 * 1000;
export const DEFAULT_PERIOD = 'week';
export const PERIOD_MS = Object.freeze({
  day: DAY_MS,
  week: 7 * DAY_MS,
  fortnight: 14 * DAY_MS,
  month: 30 * DAY_MS,
  '3month': 90 * DAY_MS,
  '6month': 180 * DAY_MS,
  year: 365 * DAY_MS,
});
export const PERIOD_ORDER = Object.freeze(['day', 'week', 'fortnight', 'month', '3month', '6month', 'year']);

// Unknown/empty → one week. Mirrors core periodMs.
export const periodMs = (period) => PERIOD_MS[period] ?? EXPIRY_MS;
// Mirrors core addPeriod.
export const addPeriod = (from, period) => from + periodMs(period);

// expiresAt of 0 (or absent) == keep forever.
export const isExpired = (meta, now) => {
  if (!meta || !meta.expiresAt) return false;
  return now > meta.expiresAt;
};

export const expiresAt = (meta) => {
  if (!meta || !meta.expiresAt) return null;
  return meta.expiresAt;
};

// Not yet expired but due within WARN_MS; already-expired projects return false.
export const isExpiringSoon = (meta, now) => {
  const at = expiresAt(meta);
  if (at == null) return false;
  return at > now && (at - now) <= WARN_MS;
};
