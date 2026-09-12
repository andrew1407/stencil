// Human-duration parser, a port of core/parse/durationParser.cpp: "days 23", "fortnight",
// "off" → milliseconds (0 = keep forever), or null for an invalid spec. Fixed durations
// (week 7d, fortnight 14d, month 30d, year 365d) match PERIOD_MS in projectsStore.

export declare const parseDuration: (spec: unknown) => number | null;
