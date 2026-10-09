import type { ApiPart } from '../apiPart.js';
/** LIMITS.cropEdgeSlackPx: how far past [0, length] a crop edge may land (core CROP_EDGE_SLACK_PX). */
export declare const CROP_EDGE_SLACK_PX: number;
export declare const createCropApi: (deps: { app: unknown }) => ApiPart;
