// Module Worker entry point — no exports; self.onmessage/self.postMessage is its whole surface.
import type { ImageRequest, ImageReply } from './imageMessages.js';

export type ImageWorkerRequest = ImageRequest;
export type ImageWorkerReply = ImageReply;
