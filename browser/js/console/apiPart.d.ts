// The shape every window.stencil facade half returns: its members, plus the hand-over
// that gives them the frozen facade so `return stencil` chains (see stencilApi.ts flow).
export interface ApiPart {
  api: Record<string | symbol, unknown>;
  setFacade(facade: unknown): void;
}
