export type DropChoice = 'replace' | 'newtab' | 'cancel';
export declare const mountDropChoice: (accent?: string, mode?: 'system' | 'dark' | 'light') => Promise<DropChoice>;
