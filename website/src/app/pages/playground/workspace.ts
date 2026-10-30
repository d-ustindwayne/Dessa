import { Injectable, computed, signal } from '@angular/core';
import { PLAYGROUND } from '../../../generated/playground';

/** A file's content: text, or bytes (a .wasm that `dessa build` made). */
export type Content = string | Uint8Array;

interface Saved {
  files: Record<string, Content>;
  folders: string[];
  open: string[];
  active: string | null;
}

const DB = 'dessa-playground';
const STORE = 'workspace';

/** "/a/b.ds" -> "/a"; "/b.ds" -> "/" */
export function parentOf(path: string): string {
  const at = path.lastIndexOf('/');
  return at <= 0 ? '/' : path.slice(0, at);
}

export function nameOf(path: string): string {
  return path.slice(path.lastIndexOf('/') + 1);
}

/** Joins and normalises: resolve("/a", "../b/c.ds") -> "/b/c.ds" */
export function resolvePath(cwd: string, path: string): string {
  const parts = (path.startsWith('/') ? path : `${cwd}/${path}`).split('/');
  const out: string[] = [];
  for (const p of parts) {
    if (!p || p === '.') continue;
    if (p === '..') out.pop();
    else out.push(p);
  }
  return '/' + out.join('/');
}

/**
 * The visitor's files, kept in their own browser (IndexedDB). Starts with the
 * starter examples; Reset brings them back.
 */
@Injectable()
export class Workspace {
  readonly files = signal<Record<string, Content>>({});
  readonly folders = signal<string[]>([]);
  readonly open = signal<string[]>([]);
  readonly active = signal<string | null>(null);
  readonly saved = signal(true);

  /** Every folder: the ones made explicitly and the ones files are in. */
  readonly allFolders = computed(() => {
    const set = new Set<string>(['/']);
    for (const f of this.folders()) set.add(f);
    for (const path of Object.keys(this.files())) {
      for (let p = parentOf(path); p !== '/'; p = parentOf(p)) set.add(p);
    }
    return [...set].sort();
  });

  private saveTimer: ReturnType<typeof setTimeout> | null = null;

  async load(): Promise<void> {
    const saved = await this.read();
    if (saved && Object.keys(saved.files).length) {
      this.files.set(saved.files);
      this.folders.set(saved.folders ?? []);
      this.open.set((saved.open ?? []).filter((p) => p in saved.files));
      this.active.set(saved.active && saved.active in saved.files ? saved.active : null);
    } else {
      this.reset(false);
    }
    if (!this.active() && this.files()['/main.ds'] !== undefined) this.openFile('/main.ds');
  }

  /** The starter examples, as on the first visit. */
  reset(save = true): void {
    this.files.set({ ...PLAYGROUND.starters });
    this.folders.set([]);
    this.open.set(['/main.ds']);
    this.active.set('/main.ds');
    if (save) this.saveNow();
  }

  isText(path: string): boolean {
    return typeof this.files()[path] === 'string';
  }

  text(path: string): string {
    const c = this.files()[path];
    return typeof c === 'string' ? c : '';
  }

  exists(path: string): boolean {
    return path in this.files() || this.allFolders().includes(path);
  }

  isFolder(path: string): boolean {
    return this.allFolders().includes(path);
  }

  write(path: string, content: Content): void {
    this.files.update((f) => ({ ...f, [path]: content }));
    this.changed();
  }

  makeFolder(path: string): void {
    if (!this.folders().includes(path)) this.folders.update((f) => [...f, path]);
    this.changed();
  }

  /** Removes a file, or a folder with everything in it. */
  remove(path: string): void {
    const inside = (p: string) => p === path || p.startsWith(path + '/');
    this.files.update((f) => Object.fromEntries(Object.entries(f).filter(([p]) => !inside(p))));
    this.folders.update((f) => f.filter((p) => !inside(p)));
    this.open.update((o) => o.filter((p) => !inside(p)));
    if (this.active() && inside(this.active()!)) this.active.set(this.open()[0] ?? null);
    this.changed();
  }

  /** Renames or moves a file or folder (and what's inside it). */
  rename(from: string, to: string): void {
    const move = (p: string) => (p === from ? to : p.startsWith(from + '/') ? to + p.slice(from.length) : p);
    this.files.update((f) => Object.fromEntries(Object.entries(f).map(([p, c]) => [move(p), c])));
    this.folders.update((f) => f.map(move));
    this.open.update((o) => o.map(move));
    if (this.active()) this.active.set(move(this.active()!));
    this.changed();
  }

  openFile(path: string): void {
    if (!this.open().includes(path)) this.open.update((o) => [...o, path]);
    this.active.set(path);
    this.changed();
  }

  closeFile(path: string): void {
    const list = this.open();
    const at = list.indexOf(path);
    this.open.set(list.filter((p) => p !== path));
    if (this.active() === path) this.active.set(this.open()[Math.max(0, at - 1)] ?? null);
    this.changed();
  }

  /** Saves soon (each change), or now. */
  changed(): void {
    this.saved.set(false);
    if (this.saveTimer) clearTimeout(this.saveTimer);
    this.saveTimer = setTimeout(() => this.saveNow(), 600);
  }

  async saveNow(): Promise<void> {
    if (this.saveTimer) clearTimeout(this.saveTimer);
    this.saveTimer = null;
    const data: Saved = { files: this.files(), folders: this.folders(), open: this.open(), active: this.active() };
    try {
      const db = await this.db();
      await new Promise<void>((resolve, reject) => {
        const tx = db.transaction(STORE, 'readwrite');
        tx.objectStore(STORE).put(data, 'workspace');
        tx.oncomplete = () => resolve();
        tx.onerror = () => reject(tx.error);
      });
      this.saved.set(true);
    } catch {
      // private browsing without IndexedDB: the files last until the page closes
    }
  }

  private async read(): Promise<Saved | null> {
    try {
      const db = await this.db();
      return await new Promise((resolve, reject) => {
        const request = db.transaction(STORE).objectStore(STORE).get('workspace');
        request.onsuccess = () => resolve((request.result as Saved) ?? null);
        request.onerror = () => reject(request.error);
      });
    } catch {
      return null;
    }
  }

  private dbPromise: Promise<IDBDatabase> | null = null;
  private db(): Promise<IDBDatabase> {
    return (this.dbPromise ??= new Promise((resolve, reject) => {
      const request = indexedDB.open(DB, 1);
      request.onupgradeneeded = () => request.result.createObjectStore(STORE);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    }));
  }
}
