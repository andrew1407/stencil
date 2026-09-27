// The options page's Provider choices and default URLs, built from providers.json.

/** `[value, label]` pairs: the off state first, then every provider in providers.json order. */
export declare const providerOptions: () => [string, string][];

/** Fills every `[data-provider-url]` node under `root` with that provider's default base URL (an input's placeholder). */
export declare const fillProviderUrls: (root: ParentNode) => void;
