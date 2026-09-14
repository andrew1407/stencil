// Shape of colorFamilies.js — the user-facing names for the things .stc colours.
export declare const FAMILY_TYPE: Record<string, string>;
export declare const FAMILIES: readonly string[];
export declare const HEX: RegExp;
export declare const DEFAULTS: { dark: Record<string, string>; light: Record<string, string> };
export declare const defaultsFor: (light: boolean) => Record<string, string>;
export declare const overridesFor: (
  setting: unknown, options?: { light?: boolean },
) => Record<string, string>;
