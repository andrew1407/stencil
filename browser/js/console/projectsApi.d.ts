import type { ApiPart } from './apiPart.js';
export declare const createProjectsApi: (deps: {
  app: unknown;
  makeProject: (id: string | null, incognito?: boolean) => unknown;
  openedIds: () => Set<unknown>;
}) => ApiPart;
