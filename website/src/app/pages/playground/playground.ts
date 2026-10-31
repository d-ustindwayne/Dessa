import { Component, ElementRef, OnDestroy, PLATFORM_ID, afterNextRender, computed, inject, signal, viewChild } from '@angular/core';
import { isPlatformBrowser } from '@angular/common';
import type { EditorState } from '@codemirror/state';
import type { EditorView } from '@codemirror/view';
import type { Terminal } from '@xterm/xterm';
import { Seo } from '../../seo';
import { PLAYGROUND } from '../../../generated/playground';
import { Shell } from './shell';
import { Toolchain } from './toolchain';
import { Workspace, nameOf, parentOf, resolvePath } from './workspace';

interface Row {
  path: string;
  name: string;
  depth: number;
  folder: boolean;
  /** the name box of a new file or folder */
  input?: boolean;
}

/** The name box in the file list: a new file or folder in the folder `path`, or renaming `path` */
interface Editing {
  kind: 'file' | 'folder' | 'rename';
  path: string;
  error: string;
}

/** The editor's keys, for the Shortcuts dialog. Mod is Ctrl (Cmd on a Mac); the third column is the Mac's when it differs. */
const SHORTCUTS: { group: string; keys: [string, string, string?][] }[] = [
  { group: 'Run and save', keys: [
    ['Run the open file', 'Mod+\\  or  Mod+Alt+.'],
    ['Save', 'Mod+S'],
    ['Stop the program (in the terminal)', 'Ctrl+C'],
    ['Clear the terminal (in the terminal)', 'Ctrl+L'],
  ] },
  { group: 'Lines', keys: [
    ['Duplicate the line down / up', 'Shift+Alt+Down / Up'],
    ['Move the line down / up', 'Alt+Down / Up'],
    ['Delete the line', 'Mod+Shift+K'],
    ['New line below', 'Mod+Enter'],
    ['Comment the line', 'Mod+/'],
    ['Block comment', 'Shift+Alt+A', 'Ctrl+Shift+A'],
    ['Indent / outdent', 'Tab / Shift+Tab'],
    ['Indent / outdent the line', 'Mod+] / Mod+['],
    ['Undo / redo', 'Mod+Z / Mod+Y', 'Mod+Z / Mod+Shift+Z'],
  ] },
  { group: 'Selection', keys: [
    ['Select the next match of the word', 'Mod+D'],
    ['Select every match', 'Mod+Shift+L'],
    ['Add a cursor below / above', 'Mod+Alt+Down / Up'],
    ['Select a column', 'Alt+Drag'],
    ['Select the line', 'Mod+L'],
    ['Select everything', 'Mod+A'],
    ['Jump to the matching bracket', 'Mod+Shift+\\'],
  ] },
  { group: 'Find', keys: [
    ['Find and replace', 'Mod+F  or  Mod+H'],
    ['Next / previous match', 'F3 / Shift+F3'],
    ['Go to line', 'Mod+G'],
  ] },
  { group: 'Code', keys: [
    ['Suggestions', 'Ctrl+Space'],
    ['Next error', 'F8'],
    ['Fold / unfold', 'Ctrl+Shift+[ / ]', 'Mod+Alt+[ / ]'],
    ['Fold all / unfold all', 'Ctrl+Alt+[ / ]'],
  ] },
  { group: 'Files (in the file list)', keys: [
    ['Rename', 'F2'],
    ['Delete', 'Delete'],
  ] },
];

@Component({
  selector: 'app-playground',
  templateUrl: './playground.html',
  styleUrl: './playground.css',
  providers: [Workspace, Toolchain],
})
export class Playground implements OnDestroy {
  readonly ws = inject(Workspace);
  readonly tools = inject(Toolchain);
  readonly version = PLAYGROUND.version;
  readonly examples = Object.keys(PLAYGROUND.starters).filter((p) => p.startsWith('/examples/'));

  readonly ready = signal(false);
  /** phones only: the file list is an overlay, shown when asked for */
  readonly sidebar = signal(false);
  readonly editing = signal<Editing | null>(null);
  /** what the confirm dialog asks, and what its button does */
  readonly asking = signal<{ text: string; ok: string; action: () => void } | null>(null);
  readonly mac = signal(false);
  readonly shortcuts = computed(() =>
    SHORTCUTS.map((g) => ({
      group: g.group,
      keys: g.keys.map(([what, keys, macKeys]) => ({
        what,
        keys: this.mac() ? (macKeys ?? keys).replace(/Mod\+/g, 'Cmd+').replace(/Alt\+/g, 'Option+') : keys.replace(/Mod\+/g, 'Ctrl+'),
      })),
    })),
  );
  readonly terminalOpen = signal(true);
  readonly sidebarWidth = signal(220);
  readonly terminalHeight = signal(260);
  readonly selected = signal('/');
  readonly expanded = signal<Set<string>>(new Set(['/', '/examples']));
  readonly copied = signal(false);
  readonly notice = signal('');

  private readonly editorHost = viewChild<ElementRef<HTMLElement>>('editor');
  private readonly terminalHost = viewChild<ElementRef<HTMLElement>>('terminal');
  private readonly nameBox = viewChild<ElementRef<HTMLInputElement>>('nameBox');
  private readonly confirmDialog = viewChild<ElementRef<HTMLDialogElement>>('confirmDialog');
  private readonly keysDialog = viewChild<ElementRef<HTMLDialogElement>>('keysDialog');

  private view: EditorView | null = null;
  private states = new Map<string, EditorState>();
  private makeState: ((path: string) => EditorState) | null = null;
  private term: Terminal | null = null;
  private fit: { fit(): void } | null = null;
  shell: Shell | null = null;
  private cleanup: (() => void)[] = [];

  /** The tree, as rows: folders first, then files, each folder's contents below it when open. */
  readonly rows = computed<Row[]>(() => {
    const files = Object.keys(this.ws.files());
    const folders = this.ws.allFolders();
    const open = this.expanded();
    const editing = this.editing();
    const rows: Row[] = [];
    const walk = (dir: string, depth: number) => {
      if (editing && editing.kind !== 'rename' && editing.path === dir) {
        rows.push({ path: '\0new', name: '', depth, folder: editing.kind === 'folder', input: true });
      }
      const prefix = dir === '/' ? '/' : dir + '/';
      const direct = (p: string) => p !== dir && p.startsWith(prefix) && !p.slice(prefix.length).includes('/');
      for (const f of folders.filter(direct).sort()) {
        rows.push({ path: f, name: nameOf(f), depth, folder: true });
        if (open.has(f)) walk(f, depth + 1);
      }
      for (const f of files.filter(direct).sort()) rows.push({ path: f, name: nameOf(f), depth, folder: false });
    };
    walk('/', 0);
    return rows;
  });

  readonly clangText = computed(() => {
    const c = this.tools.clang();
    if (c.state === 'ready') return 'C compiler ready';
    if (c.state === 'loading') return c.total ? `C compiler ${Math.round((100 * c.done) / c.total)}%` : 'C compiler loading';
    if (c.state === 'failed') return 'C compiler failed to load';
    return 'C compiler: loads on the first run';
  });

  constructor() {
    inject(Seo).set({
      title: 'Playground',
      description: 'Write and run Dessa in your browser: the real Dessa compiler and clang, compiled to WebAssembly, with a file explorer, an editor with completions and a terminal.',
      path: '/playground',
    });
    if (!isPlatformBrowser(inject(PLATFORM_ID))) return;
    afterNextRender(() => void this.start());
  }

  private async start(): Promise<void> {
    // input() needs SharedArrayBuffer: the service worker adds the headers that
    // allow it (GitHub Pages can't), then reloads the page once
    if (!crossOriginIsolated && 'serviceWorker' in navigator && !sessionStorage.getItem('dessa-coi-tried')) {
      sessionStorage.setItem('dessa-coi-tried', '1');
      const script = document.createElement('script');
      script.src = '/playground/coi-serviceworker.js';
      document.head.append(script);
    }
    this.mac.set(/Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent));
    await this.ws.load();
    const [{ EditorState }, view, commands, language, search, autocomplete, lint, xterm, fit, dessa] = await Promise.all([
      import('@codemirror/state'),
      import('@codemirror/view'),
      import('@codemirror/commands'),
      import('@codemirror/language'),
      import('@codemirror/search'),
      import('@codemirror/autocomplete'),
      import('@codemirror/lint'),
      import('@xterm/xterm'),
      import('@xterm/addon-fit'),
      import('./dessa-language'),
    ]);

    // ---- the editor ----
    const hooks = (path: string) => ({
      path: () => path,
      check: async () => (await this.tools.diagnostics(path.slice(1), this.filesNow())),
      complete: async (line: number, column: number) => (await this.tools.command(['complete', path.slice(1), '--at', `${line}:${column}`], this.filesNow())).out,
      hover: async (line: number, column: number) => (await this.tools.command(['hover', path.slice(1), '--at', `${line}:${column}`], this.filesNow())).out,
      symbols: async (file = path.slice(1)) => (await this.tools.command(['symbols', file], this.filesNow())).out,
      modules: () => this.modulesFor(path),
    });
    const base = [
      view.lineNumbers(),
      view.highlightActiveLineGutter(),
      view.highlightSpecialChars(),
      commands.history(),
      language.foldGutter(),
      view.drawSelection(),
      view.dropCursor(),
      EditorState.allowMultipleSelections.of(true),
      language.indentOnInput(),
      language.bracketMatching(),
      autocomplete.closeBrackets(),
      view.rectangularSelection(),
      view.highlightActiveLine(),
      search.highlightSelectionMatches(),
      lint.lintGutter(),
      dessa.editorTheme,
      view.keymap.of([
        // as in VS Code
        { key: 'Mod-g', run: search.gotoLine, preventDefault: true },
        { key: 'Mod-l', run: commands.selectLine, preventDefault: true },
        { key: 'Mod-h', run: search.openSearchPanel, preventDefault: true },
        ...autocomplete.closeBracketsKeymap,
        ...commands.defaultKeymap,
        ...search.searchKeymap,
        ...commands.historyKeymap,
        ...language.foldKeymap,
        ...autocomplete.completionKeymap,
        ...lint.lintKeymap,
        commands.indentWithTab,
      ]),
      language.indentUnit.of('    '),
      EditorState.tabSize.of(4),
    ];
    this.makeState = (path: string) =>
      EditorState.create({
        doc: this.ws.text(path),
        extensions: [
          ...base,
          ...(path.endsWith('.ds') || path.endsWith('.dm') ? dessa.dessaSupport(hooks(path)) : []),
          view.EditorView.updateListener.of((u) => {
            if (u.docChanged && this.ws.active() === path) this.ws.write(path, u.state.doc.toString());
          }),
        ],
      });
    this.view = new view.EditorView({ parent: this.editorHost()!.nativeElement });
    this.showActive();

    // ---- the terminal ----

    const term = new xterm.Terminal({
      fontFamily: 'ui-monospace, "Cascadia Code", Consolas, "Liberation Mono", monospace',
      fontSize: 13,
      cursorBlink: true,
      convertEol: false,
      scrollback: 5000,
      theme: this.terminalTheme(),
    });
    const fitter = new fit.FitAddon();
    term.loadAddon(fitter);
    term.open(this.terminalHost()!.nativeElement);
    this.term = term;
    this.fit = fitter;
    this.shell = new Shell(term, this.ws, this.tools, () => this.afterFilesChanged());
    this.shell.start();
    requestAnimationFrame(() => fitter.fit());
    const resize = new ResizeObserver(() => this.fit?.fit());
    resize.observe(this.terminalHost()!.nativeElement);
    this.cleanup.push(() => resize.disconnect());

    // ---- keys: Ctrl+\ or Ctrl+Alt+. runs, Ctrl+S saves ----
    const keys = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && (e.key === '\\' || (e.altKey && (e.key === '.' || e.code === 'Period')))) {
        e.preventDefault();
        void this.run();
      } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
        e.preventDefault();
        void this.save();
      }
    };
    window.addEventListener('keydown', keys);
    this.cleanup.push(() => window.removeEventListener('keydown', keys));
    const scheme = matchMedia('(prefers-color-scheme: dark)');
    const retheme = () => term.options.theme = this.terminalTheme();
    scheme.addEventListener('change', retheme);
    this.cleanup.push(() => scheme.removeEventListener('change', retheme));

    this.tools.warmUp(this.ws.files());
    this.ready.set(true);
  }

  private terminalTheme() {
    const dark = matchMedia('(prefers-color-scheme: dark)').matches;
    return {
      background: dark ? '#1a1e1a' : '#f2f0e4',   // styles.css --code-bg
      foreground: dark ? '#e3e3dc' : '#1b1c18',
      cursor: dark ? '#7fcf9c' : '#14743c',
      selectionBackground: dark ? '#2b3a2f' : '#cdebd5',
      green: dark ? '#7fcf9c' : '#14743c',
      red: dark ? '#f2b8b5' : '#b3261e',
      yellow: dark ? '#d2cb71' : '#7a5d00',
      brightBlack: dark ? '#8d9488' : '#6f7468',
    };
  }

  ngOnDestroy(): void {
    for (const c of this.cleanup) c();
    this.tools.stop();
    this.view?.destroy();
    this.term?.dispose();
  }

  /** The workspace as it is right now (the editor's text included). */
  private filesNow() {
    return this.ws.files();
  }

  /** The Dessa files `path` can import, as import specs (lib.util) -> workspace path */
  private modulesFor(path: string): Record<string, string> {
    const dir = parentOf(path);
    const prefix = dir === '/' ? '/' : dir + '/';
    const out: Record<string, string> = {};
    for (const f of Object.keys(this.ws.files()).sort()) {
      if (f === path || !f.startsWith(prefix) || !/\.(ds|dm|dx|dz|dsx|dxx|dzx|dds|dss)$/.test(f)) continue;
      out[f.slice(prefix.length).replace(/\.[^.]+$/, '').replace(/\//g, '.')] = f.slice(1);
    }
    return out;
  }

  // ---- editor tabs ----
  private showActive(focus = true): void {
    const path = this.ws.active();
    if (!this.view || !this.makeState) return;
    if (!path || !this.ws.isText(path)) {
      this.view.setState(this.makeState('/'));   // empty
      return;
    }
    let state = this.states.get(path);
    if (!state || state.doc.toString() !== this.ws.text(path)) state = this.makeState(path);
    this.view.setState(state);
    if (focus) this.view.focus();
  }

  private narrow(): boolean {
    return matchMedia('(max-width: 760px)').matches;
  }

  /** `focus`: the editor takes the keys (a file clicked in the list keeps them there: F2, Delete) */
  open(path: string, focus = true): void {
    if (this.narrow()) this.sidebar.set(false);
    this.rememberState();
    this.ws.openFile(path);
    this.selected.set(path);
    this.showActive(focus);
  }

  close(path: string, event?: Event): void {
    event?.stopPropagation();
    this.rememberState();
    this.states.delete(path);
    this.ws.closeFile(path);
    this.showActive();
  }

  private rememberState(): void {
    const active = this.ws.active();
    if (this.view && active) this.states.set(active, this.view.state);
  }

  /** After a command changed files: the open editor shows the new text. */
  private afterFilesChanged(): void {
    const active = this.ws.active();
    if (active && this.view && this.ws.isText(active) && this.view.state.doc.toString() !== this.ws.text(active)) {
      this.states.delete(active);
      this.showActive();
    }
    for (const p of this.ws.open()) if (!(p in this.ws.files())) this.close(p);
  }

  // ---- explorer ----
  toggle(row: Row): void {
    this.selected.set(row.path);
    if (!row.folder) {
      this.open(row.path, this.narrow());
      return;
    }
    this.expanded.update((s) => {
      const n = new Set(s);
      if (n.has(row.path)) n.delete(row.path);
      else n.add(row.path);
      return n;
    });
  }

  /** The folder new things go in: the selected folder, or the selected file's. */
  private targetFolder(): string {
    const s = this.selected();
    return this.ws.isFolder(s) ? s : parentOf(s);
  }

  // A name box in the list, as in VS Code: Enter makes the file or folder
  // (or renames), Escape or leaving an empty box doesn't
  newFile(): void {
    this.startEditing('file', this.targetFolder());
  }

  newFolder(): void {
    this.startEditing('folder', this.targetFolder());
  }

  renameSelected(): void {
    const path = this.selected();
    if (path === '/' || !this.ws.exists(path)) return;
    this.startEditing('rename', path);
  }

  private startEditing(kind: Editing['kind'], path: string): void {
    if (kind !== 'rename') this.expanded.update((s) => new Set([...s, path]));
    this.editing.set({ kind, path, error: '' });
    setTimeout(() => {
      const box = this.nameBox()?.nativeElement;
      if (!box) return;
      box.focus();
      // the name without its extension is selected, as in VS Code
      const dot = box.value.lastIndexOf('.');
      box.setSelectionRange(0, dot > 0 ? dot : box.value.length);
    });
  }

  /** What's wrong with the name typed, or '' */
  private nameProblem(e: Editing, name: string): string {
    if (/[\\:*?"<>|]/.test(name)) return 'A name can\'t have \\ : * ? " < > or | in it';
    const target = resolvePath(e.kind === 'rename' ? parentOf(e.path) : e.path, name);
    if (target === '/') return 'Give it a name';
    if (target !== e.path && this.ws.exists(target)) return `${name} already exists here`;
    return '';
  }

  editTyped(name: string): void {
    const e = this.editing();
    if (e) this.editing.set({ ...e, error: name.trim() ? this.nameProblem(e, name.trim()) : '' });
  }

  editKey(event: KeyboardEvent, name: string): void {
    const tree = (event.target as HTMLElement).closest<HTMLElement>('.pg-tree');
    if (event.key === 'Escape') {
      event.preventDefault();
      this.editing.set(null);
    } else if (event.key === 'Enter') {
      event.preventDefault();
      this.finishEditing(name, true);
    } else return;
    // the list keeps the keys once the box is gone (a new file's editor takes them)
    if (!this.editing() && !tree?.ownerDocument.activeElement?.closest('.cm-editor')) tree?.focus();
  }

  /** Enter keeps a box whose name has a problem open; leaving it gives up. */
  finishEditing(name: string, enter = false): void {
    const e = this.editing();
    if (!e) return;
    name = name.trim();
    const problem = name ? this.nameProblem(e, name) : '';
    if (problem && enter) {
      this.editing.set({ ...e, error: problem });
      return;
    }
    this.editing.set(null);
    if (!name || problem) return;
    if (e.kind === 'rename') {
      const to = resolvePath(parentOf(e.path), name);
      if (to === e.path) return;
      this.rememberState();
      this.states.clear();
      this.ws.rename(e.path, to);
      this.selected.set(to);
      this.showActive();
    } else if (e.kind === 'file') {
      const path = resolvePath(e.path, name);
      this.ws.write(path, path.endsWith('.ds') ? 'print("Hello from Dessa");\n' : '');
      this.expanded.update((s) => new Set([...s, parentOf(path)]));
      this.open(path);
    } else {
      const path = resolvePath(e.path, name);
      this.ws.makeFolder(path);
      this.expanded.update((s) => new Set([...s, parentOf(path), path]));
      this.selected.set(path);
    }
  }

  nameBoxValue(event: Event): string {
    return (event.target as HTMLInputElement).value;
  }

  isRenaming(path: string): boolean {
    const e = this.editing();
    return e?.kind === 'rename' && e.path === path;
  }

  /** F2 renames, Delete deletes, as in VS Code's explorer */
  treeKey(event: KeyboardEvent): void {
    if (this.editing()) return;
    if (event.key === 'F2') {
      event.preventDefault();
      this.renameSelected();
    } else if (event.key === 'Delete') {
      event.preventDefault();
      this.deleteSelected();
    }
  }

  deleteSelected(): void {
    const path = this.selected();
    if (path === '/' || !this.ws.exists(path)) return;
    this.ask(`Delete ${path.slice(1)}${this.ws.isFolder(path) ? ' and everything in it' : ''}?`, 'Delete', () => {
      this.states.delete(path);
      this.ws.remove(path);
      this.selected.set(parentOf(path));
      this.showActive();
    });
  }

  // ---- dialogs ----
  private ask(text: string, ok: string, action: () => void): void {
    this.asking.set({ text, ok, action });
    this.confirmDialog()?.nativeElement.showModal();
  }

  answer(yes: boolean): void {
    const asked = this.asking();
    this.confirmDialog()?.nativeElement.close();
    if (yes) asked?.action();
  }

  showShortcuts(): void {
    this.keysDialog()?.nativeElement.showModal();
  }

  /** A click on the dialog's backdrop (the <dialog> itself, outside its box) closes it. */
  backdropClick(event: MouseEvent, dialog: HTMLDialogElement): void {
    if (event.target !== dialog) return;
    const box = dialog.getBoundingClientRect();
    if (event.clientX < box.left || event.clientX > box.right || event.clientY < box.top || event.clientY > box.bottom) dialog.close();
  }

  // ---- actions ----
  async run(): Promise<void> {
    const path = this.ws.active();
    if (!path || !this.shell) return;
    if (!path.endsWith('.ds')) return this.say('Open a .ds file to run it');
    if (this.tools.running() || this.shell.isBusy) return this.say('Something is still running (Ctrl+C in the terminal stops it)');
    this.terminalOpen.set(true);
    await this.save();
    await this.shell.type(`dessa run ${path.slice(1)}`);
  }

  stop(): void {
    this.tools.stop();
  }

  async save(): Promise<void> {
    await this.ws.saveNow();
    this.say('Saved in this browser');
  }

  async copy(): Promise<void> {
    const path = this.ws.active();
    if (!path) return;
    try {
      await navigator.clipboard.writeText(this.ws.text(path));
      this.copied.set(true);
      setTimeout(() => this.copied.set(false), 1500);
    } catch {
      this.say('The browser didn\'t allow copying');
    }
  }

  reset(): void {
    this.ask('Put back the starter files? Your own files and changes in this playground are deleted.', 'Reset', () => {
      this.states.clear();
      this.ws.reset();
      this.selected.set('/main.ds');
      this.showActive();
    });
  }

  clearTerminal(): void {
    this.shell?.clear();
  }

  openExample(path: string): void {
    if (!path) return;
    if (!(path in this.ws.files())) this.ws.write(path, (PLAYGROUND.starters as Record<string, string>)[path]);
    this.expanded.update((s) => new Set([...s, '/examples']));
    this.open(path);
  }

  private say(text: string): void {
    this.notice.set(text);
    setTimeout(() => this.notice() === text && this.notice.set(''), 2500);
  }

  // ---- resizing ----
  drag(event: PointerEvent, what: 'sidebar' | 'terminal'): void {
    event.preventDefault();
    const startX = event.clientX, startY = event.clientY;
    const startWidth = this.sidebarWidth(), startHeight = this.terminalHeight();
    const move = (e: PointerEvent) => {
      if (what === 'sidebar') this.sidebarWidth.set(Math.max(140, Math.min(480, startWidth + e.clientX - startX)));
      else this.terminalHeight.set(Math.max(90, Math.min(window.innerHeight - 220, startHeight - (e.clientY - startY))));
    };
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      this.fit?.fit();
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  }

  isOpenFolder(path: string): boolean {
    return this.expanded().has(path);
  }

  nameOf = nameOf;
}
