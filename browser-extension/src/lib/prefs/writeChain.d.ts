export type WriteTurn = <T>(fn: () => Promise<T>) => Promise<T>;
export type StoreWriteMessage = { type: string; store: 'pins' | 'ledger'; op: string; args: unknown[] };

export declare function writeChain(): WriteTurn;
export declare function routeStoreWrites(send?: ((msg: StoreWriteMessage) => Promise<unknown>) | null): void;
export declare function viaWorker<T>(
  store: 'pins' | 'ledger', op: string, args: unknown[], local: (...args: any[]) => Promise<T>,
): Promise<T>;
