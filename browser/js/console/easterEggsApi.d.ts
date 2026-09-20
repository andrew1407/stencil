import type { ApiPart } from './apiPart.js';
import type { DrawingApp } from '../core/drawingApp.js';

export declare const createEasterEggsApi: (deps: { app: DrawingApp; guard: <T>(obj: T) => T }) => ApiPart;
