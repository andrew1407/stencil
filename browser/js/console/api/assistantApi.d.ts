import type { ApiPart } from '../apiPart.js';
export declare const createAssistantApi: (deps: {
  app: unknown;
  guard: <T extends object>(obj: T) => T;
}) => ApiPart;
