// Project lifecycle + local ↔ server transfer: switching and opening projects, remove and
// clear, and the deps every part of the subsystem reads. The meta writes live in
// projectMetaOps.js, the move/copy flows in serverTransfer.js — this delegates.
import type { Storage } from '../storage/storage.js';
import type { TabsCoordinator } from '../launch/tabsCoordinator.js';
import type { RemoteSyncController, RemoteLink } from '../remote/syncController.js';
import type { ProjectMeta, ProjectLayout } from './store/projectsStore.js';
import type { ConnectionManager } from '../../net/connectionManager.js';
import type { RemoteProjectRecord } from '../../net/serverConnection.js';
import type { CopyProjectCall } from './copy/copyProject.js';
import type { CopySource } from './copy/source.js';

/** The narrow app facade the controller drives (mirrors the desktop twin's Hooks). */
export interface ProjectTransferHost {
  activeProjectId: string | null;
  remoteLink: RemoteLink | null;
  blankColor: string;
  imageBaseName: string | null;
  readonly chatPersistence?: {
    projectOpened(id: string | null): void;
    projectRemoved(id: string): void;
    allProjectsCleared(): void;
    /** A local id or a { conn, remoteId } server chat file, on each side. */
    projectCopied?(from: object, to: object): Promise<boolean>;
  } | null;
  /** The live editor as a registry row + payload (copy/source.js liveProjectSnapshot). */
  liveProject(): Omit<CopySource, 'remote'>;
  updateProjectTitle(): void;
  updateIncognitoUI(): void;
  newEditor(): void;
  loadImageFromFile(file: File, opts?: LoadImageOpts): void;
  setBlankColor(color: string): void;
}

/** What a remote open hands to loadImageFromFile alongside the bytes. */
export interface LoadImageOpts {
  source?: string;
  resource?: string;
  color?: string;
  address?: string;
  remoteId?: string;
  version?: number;
  layout?: ProjectLayout | null;
  name?: string;
}

/** A server listing row, as ServerConnection.tagRemote stamps it. */
export interface RemoteProjectMeta extends RemoteProjectRecord { serverUrl: string; }

export interface ProjectTransferDeps {
  storage: Storage;
  tabs: TabsCoordinator;
  remoteSync: RemoteSyncController;
  getConnections: () => ConnectionManager;
  host: ProjectTransferHost;
}

/** What removing `n` projects says: 'Project cleared' for one, 'Projects cleared' for more. */
export declare const clearedToast: (n: number) => string;

export declare class ProjectTransferController {
  constructor(deps: ProjectTransferDeps);
  storage: Storage;
  tabs: TabsCoordinator;
  remoteSync: RemoteSyncController;
  getConnections: () => ConnectionManager;
  host: ProjectTransferHost;
  /** Persist the current project, then load `id`; false when unknown or already active. */
  switchToProject(id: string): boolean;
  openProjectInNewTab(id: string | null, win?: Window | null): void;
  openRemoteProjectInNewTab(meta: RemoteProjectMeta | null, win?: Window | null): void;
  /** Any `#stencil=` hand-off payload in a new tab. */
  openLaunchInNewTab(payload: object, win?: Window | null): void;
  /** Switches to an already-linked local twin rather than downloading a duplicate. */
  openRemoteProject(meta: RemoteProjectMeta): Promise<void>;
  renewProject(id: string): ProjectMeta | null;
  setProjectExpiration(id: string, opts?: { expiresAt?: number; refreshPeriod?: string; autoRefresh?: boolean }): ProjectMeta | null;
  closeProject(id: string | null, opts?: { fully?: boolean }): void;
  removeProject(id: string): void;
  clearAllProjects(): void;
  renameProject(id: string, name: string): ProjectMeta | null;
  pushProjectFieldToServer(id: string, fields: Record<string, unknown>, failMsg: string): Promise<void>;
  setProjectColor(id: string, color: string): ProjectMeta | null;
  setProjectKeywords(id: string, keywords: string | string[]): ProjectMeta | null;
  setProjectDescription(id: string, description: string): ProjectMeta | null;
  setProjectBlankColor(id: string, color: string): ProjectMeta | null;
  moveProjectToServer(id: string, address: string): Promise<string>;
  copyProjectToServer(id: string, address: string, opts?: { name?: string }): Promise<string>;
  moveProjectToLocal(m: RemoteProjectMeta): Promise<string>;
  copyServerProjectToLocal(m: RemoteProjectMeta, opts?: { name?: string }): Promise<string>;
  copyServerProjectToIncognito(m: RemoteProjectMeta, opts?: { newTab?: boolean }): Promise<unknown>;
  /** "Make a copy": the new local id, the server copy's remote id, or null for an incognito copy. */
  copyProject(call: CopyProjectCall): Promise<string | null>;
}
