export interface StaticServer { url: string; stop: () => void }

export function startAppServer(): Promise<StaticServer>;
export function siteUrl(port: number): string;
export function startSiteServer(port: number): Promise<StaticServer>;
