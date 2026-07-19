// ── StencilSync: live two-way sync between a project and its linked .stencil file ──────────
// Opt-in per session (Chromium File System Access only): debounced auto-save on edit + polled
// watch that applies external writes in place, or prompts (mine/theirs/merge) on a conflict.
import { parseProjectFile, serializeProjectFile } from './projectFile.js';

const LIVE_KEY = 'drawingApp_stencilLiveSync';
const POLL_MS = 2000;
const DEBOUNCE_MS = 800;

// Pure watch-loop classifier (unit-tested) over 3 texts (baseline ancestor / current editor /
// external file): 'none' | 'local' (editor only) | 'external' (file only) | 'conflict' (both).
export const classifyFileChange = (baseline, current, external) => {
  const fileChanged = external !== baseline;
  const localChanged = current !== baseline;
  if (!fileChanged) return localChanged ? 'local' : 'none';
  return localChanged ? 'conflict' : 'external';
};

export class StencilSync {
  constructor(app) {
    this.app = app;
    this.handle = null;          // the linked FileSystemFileHandle (null = not file-linked)
    this.baseline = null;        // the text we last wrote/read — the sync common ancestor
    this.name = '';              // linked file name (for the UI)
    this.#debounce = null;
    this.#poll = null;
    this.busy = false;           // guards overlapping write/apply passes
    this.lastMod = null;         // linked file's last-seen mtime/size — lets the poll skip the
    this.lastSize = null;        // (potentially multi-MB) text read when the file is untouched
  }

  #debounce;
  #poll;

  // File System Access (retainable handle) — the whole feature needs it.
  get supported() { return typeof window !== 'undefined' && typeof window.showOpenFilePicker === 'function'; }
  get linked() { return !!this.handle; }
  get liveSync() { try { return localStorage.getItem(LIVE_KEY) === '1'; } catch { return false; } }
  set liveSync(on) {
    try { localStorage.setItem(LIVE_KEY, on ? '1' : '0'); } catch { /* storage blocked */ }
    if (on && this.linked) this.startPoll(); else this.stopPoll();
    this.app.updateStencilSyncUI?.();
  }

  // Link (or relink) the active project to `handle`; the file's current text becomes the baseline.
  async link(handle, name = '') {
    this.handle = handle;
    this.name = name || handle?.name || '.stencil';
    this.baseline = await this.#read();
    if (this.liveSync) this.startPoll();
    this.app.updateStencilSyncUI?.();
  }

  unlink() {
    this.stopPoll();
    clearTimeout(this.#debounce);
    this.handle = null;
    this.baseline = null;
    this.name = '';
    this.app.updateStencilSyncUI?.();
  }

  // The editor's project serialized to .stencil text (the thing we compare/write).
  #current() { return serializeProjectFile(this.app.projectFileState({ includeTheme: true })); }

  async #read() {
    try {
      const file = await this.handle.getFile();
      this.lastMod = file.lastModified; this.lastSize = file.size;
      return await file.text();
    } catch { return null; }
  }
  async #ensurePermission() {
    if (!this.handle?.queryPermission) return true;   // older/no-perm impls: assume granted
    if (await this.handle.queryPermission({ mode: 'readwrite' }) === 'granted') return true;
    return await this.handle.requestPermission({ mode: 'readwrite' }) === 'granted';
  }
  async #write(text) {
    if (!(await this.#ensurePermission())) return false;
    const w = await this.handle.createWritable();
    await w.write(text);
    await w.close();
    this.baseline = text;
    // Refresh the mtime/size fingerprint so the next poll doesn't re-read our own write.
    try { const f = await this.handle.getFile(); this.lastMod = f.lastModified; this.lastSize = f.size; } catch { /* keep old */ }
    return true;
  }

  // ── auto-save (edit → debounced write-back) ────────────────────────────────
  onEdit() {
    if (!this.linked || !this.liveSync) return;
    clearTimeout(this.#debounce);
    this.#debounce = setTimeout(() => { this.flush(); }, DEBOUNCE_MS);
  }
  async flush() {
    if (!this.linked || !this.liveSync || this.busy) return;
    const text = this.#current();
    if (text === this.baseline) return;                 // no local change
    const ext = await this.#read();
    if (ext != null && ext !== this.baseline) { await this.#onExternal(ext); return; }  // race: external changed
    this.busy = true;
    try {
      if (await this.#write(text)) this.app.showSaveStatus?.('Synced to file', 'var(--success)', 'check');
    } finally { this.busy = false; }
  }

  // ── watch (poll → apply / prompt) ──────────────────────────────────────────
  startPoll() { if (this.#poll || !this.linked) return; this.#poll = setInterval(() => this.check(), POLL_MS); }
  stopPoll() { clearInterval(this.#poll); this.#poll = null; }
  async check() {
    if (!this.linked || !this.liveSync || this.busy) return;
    // Cheap metadata probe first: if the file's mtime + size are unchanged since we last
    // saw it, skip reading its (image-bearing, possibly multi-MB) contents entirely.
    let file;
    try { file = await this.handle.getFile(); } catch { return; }
    if (this.lastMod != null && file.lastModified === this.lastMod && file.size === this.lastSize) return;
    this.lastMod = file.lastModified; this.lastSize = file.size;
    let ext;
    try { ext = await file.text(); } catch { return; }
    if (ext === this.baseline) return;
    await this.#onExternal(ext);
  }

  async #onExternal(ext) {
    this.busy = true;
    try {
      const kind = classifyFileChange(this.baseline, this.#current(), ext);
      if (kind === 'external') {
        this.#apply(ext);
        this.baseline = ext;
        this.app.showSaveStatus?.('Updated from file', 'var(--success)', 'info');
      } else if (kind === 'conflict') {
        await this.#resolve(ext);
      }
      // 'none'/'local' — nothing to pull.
    } finally { this.busy = false; }
  }

  #apply(ext, opts = {}) {
    const res = parseProjectFile(ext);
    if (res.ok) this.app.applyProjectFileInPlace(res.project, opts);
  }

  async #resolve(ext) {
    // Prompt: keep mine (overwrite file) / take theirs (reload) / merge lines.
    const choice = await this.app.chooseFileConflict?.(this.name);
    if (choice === 'theirs') {
      this.#apply(ext);
      this.baseline = ext;
      this.app.showSaveStatus?.('Reloaded from file', 'var(--success)', 'info');
    } else if (choice === 'merge') {
      this.#apply(ext, { mergeLines: true });
      const merged = this.#current();
      await this.#write(merged);
      this.app.showSaveStatus?.('Merged with file', 'var(--success)', 'check');
    } else if (choice === 'mine') {
      await this.#write(this.#current());
      this.app.showSaveStatus?.('Kept your version', 'var(--success)', 'check');
    }
    // dismissed → leave the divergence; the next edit/poll re-prompts.
  }
}
