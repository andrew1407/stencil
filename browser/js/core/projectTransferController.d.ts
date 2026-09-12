// Project lifecycle + local ↔ server transfer: switching and opening projects, remove and
// clear, and the deps every part of the subsystem reads. The meta writes live in
// projectMetaOps.js, the move/copy flows in projectServerTransfer.js — this delegates.
import type { Storage } from './storage.js';
import type { TabsCoordinator } from './tabsCoordinator.js';
import type { RemoteSyncController, RemoteLink } from './remoteSyncController.js';
import type { ProjectMeta, ProjectLayout } from './projectsStore.js';
import type { ConnectionManager } from '../net/connectionManager.js';
import type { RemoteProjectRecord } from '../net/serverConnection.js';

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
  } | null;
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
}
