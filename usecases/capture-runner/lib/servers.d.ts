export interface StaticServer { url: string; stop: () => void }

export function startAppServer(): Promise<StaticServer>;
export interface MediaServer { url: (name: string) => string; stop: () => void }
export function startMediaServer(dir: string, port: number): Promise<MediaServer>;
export function siteUrl(port: number): string;
export function startSiteServer(port: number): Promise<StaticServer>;
