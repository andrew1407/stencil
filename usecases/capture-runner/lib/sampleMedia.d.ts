export interface ClipSpec { seconds: number; fps: number; size: string; name: string }
export const CLIP: Readonly<ClipSpec>;
export function sampleVideo(spec?: ClipSpec): { dir: string; file: string; name: string };
