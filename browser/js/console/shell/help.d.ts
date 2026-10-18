// The /help text, laid out as the CLI's.
export interface HelpLine { kind: 'plain' | 'blank' | 'section' | 'row'; text: string; head?: string; help?: string; }
export declare const HELP_GUTTER: number;
export declare const helpLines: (opts?: { cliMode?: boolean }) => HelpLine[];
