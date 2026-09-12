// Pure, DOM-free project registry over a localStorage-like backend: a registry array of
// metadata plus one payload key per project. Every JSON read is guarded with a safe
// default; QuotaExceededError from a write propagates so the DOM adapter can evict + retry.
import type { CodecLine } from './linesCodec.js';

/** A refresh preset; fixed durations so this and core/state/projectsStore.cpp agree. */
export type RefreshPeriod = 'day' | 'week' | 'fortnight' | 'month' | '3month' | '6month' | 'year';

/** One registry row. Absent fields are default-filled on read (see normalizeMeta). */
export interface ProjectMeta {
  id: string;
  name: string;
  color: string;
  description: string;
  keywords: string[];
  thumbnail: string | null;
  createdAt: number;
  updatedAt: number;
  /** Epoch ms; 0 = keep forever. */
  expiresAt: number;
  refreshPeriod: RefreshPeriod;
  autoRefresh: boolean;
  hasImage: boolean;
  imageW: number | null;
  imageH: number | null;
  lineLengthCm: number;
  blank: boolean;
  blankColor: string;
  source?: string | null;
  resource?: string | null;
  fromFile?: boolean;
  /** Set for a server-linked project (see RemoteLink in remoteSyncController). */
  address?: string | null;
  remoteId?: string | null;
  remoteVersion?: number;
}

/** The stored layout: the full session serialisation (core/layout.js LAYOUT_FIELDS). */
export interface ProjectLayout {
  lines?: CodecLine[];
  imageWidth?: number;
  imageHeight?: number;
  imageBaseName?: string | null;
  imageExt?: string | null;
  imageSource?: string | null;
  imageResource?: string | null;
  cropRect?: { x: number; y: number; w?: number; h?: number; width?: number; height?: number } | null;
  rotationQuarters?: number;
  [field: string]: unknown;
}

export interface ProjectPayload { image: string | null; layout: ProjectLayout; }

export interface StoredProject { meta: ProjectMeta; payload: ProjectPayload; }

/** The backend shape: localStorage, or the Map-backed test shim exposing keys(). */
export interface StorageBackend {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem(key: string): void;
  keys?(): Iterable<string>;
}

export declare const REGISTRY_KEY: string;
export declare const PROJECT_PREFIX: string;
export declare const MIGRATED_FLAG: string;
/** One week in ms (also the "week" preset). */
export declare const EXPIRY_MS: number;
/** Warn once a project is within a day of expiry. */
export declare const WARN_MS: number;
export declare const DEFAULT_PERIOD: RefreshPeriod;
export declare const PERIOD_MS: Record<RefreshPeriod, number>;
export declare const PERIOD_ORDER: readonly RefreshPeriod[];

/** Trim, drop blanks, dedupe case-insensitively (first-seen order) — the server's joinKeywords. */
export declare const normalizeKeywords: (keywords: unknown) => string[];
export declare const periodMs: (period: string | null | undefined) => number;
export declare const addPeriod: (from: number, period: string | null | undefined) => number;
/** Persist only when there is an active, non-temporary project to write to. */
export declare const shouldPersist: (activeId: string | null | undefined, temporary: boolean) => boolean;
/** Strip a trailing copy suffix " (N)". */
export declare const baseProjectName: (name: unknown) => string;

export declare class ProjectsStore {
  constructor(storage?: StorageBackend | null);
  /** All projects, most-recently-updated first; [] on any error. */
  list(): ProjectMeta[];
  getMeta(id: string): ProjectMeta | null;
  get(id: string): StoredProject | null;
  createId(): string;
  nameExists(name: unknown, exceptId?: string | null): boolean;
  validateName(name: unknown, exceptId?: string | null): { ok: boolean; reason: string };
  rename(id: string, name: string): ProjectMeta | null;
  setColor(id: string, color: string): ProjectMeta | null;
  setKeywords(id: string, keywords: unknown): ProjectMeta | null;
  setDescription(id: string, description: unknown): ProjectMeta | null;
  setBlankColor(id: string, color: string): ProjectMeta | null;
  /** The idle-time thumbnail landing after the save that scheduled it. */
  setThumbnail(id: string, dataUrl: string): ProjectMeta | null;
  findByImage(source: string | null | undefined, name: string | null | undefined): ProjectMeta[];
  copyName(baseName: string | null | undefined, source: string | null | undefined): string;
  defaultName(): string;
  /** Writes the payload first, so a quota failure leaves the registry untouched. */
  upsert(meta: Partial<ProjectMeta> & { id: string }, payload: ProjectPayload): ProjectMeta;
  touch(id: string, now?: number): ProjectMeta | null;
  remove(id: string): void;
  clearAll(): void;
  isExpired(meta: ProjectMeta | null | undefined, now?: number): boolean;
  expiresAt(meta: ProjectMeta | null | undefined): number | null;
  isExpiringSoon(meta: ProjectMeta | null | undefined, now?: number): boolean;
  renew(id: string, now?: number): ProjectMeta | null;
  setExpiration(id: string, opts?: { expiresAt?: number; refreshPeriod?: RefreshPeriod | string; autoRefresh?: boolean }): ProjectMeta | null;
  sweepExpired(now?: number): string[];
  migrateLegacy(now?: number): string | null;
}
