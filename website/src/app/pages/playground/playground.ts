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
}

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
  readonly sidebar = signal(true);
  readonly terminalOpen = signal(true);
  readonly sidebarWidth = signal(220);
  readonly terminalHeight = signal(260);
  readonly selected = signal('/');
  readonly expanded = signal<Set<string>>(new Set(['/', '/examples']));
  readonly copied = signal(false);
  readonly notice = signal('');

  private readonly editorHost = viewChild<ElementRef<HTMLElement>>('editor');
  private readonly terminalHost = viewChild<ElementRef<HTMLElement>>('terminal');

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
    const rows: Row[] = [];
    const walk = (dir: string, depth: number) => {
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
    // on a phone the file list is an overlay: closed until asked for
    if (this.narrow()) this.sidebar.set(false);
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
      view.keymap.of([
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

  // ---- editor tabs ----
  private showActive(): void {
    const path = this.ws.active();
    if (!this.view || !this.makeState) return;
    if (!path || !this.ws.isText(path)) {
      this.view.setState(this.makeState('/'));   // empty
      return;
    }
    let state = this.states.get(path);
    if (!state || state.doc.toString() !== this.ws.text(path)) state = this.makeState(path);
    this.view.setState(state);
    this.view.focus();
  }

  private narrow(): boolean {
    return matchMedia('(max-width: 760px)').matches;
  }

  open(path: string): void {
    if (this.narrow()) this.sidebar.set(false);
    this.rememberState();
    this.ws.openFile(path);
    this.selected.set(path);
    this.showActive();
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
      this.open(row.path);
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

  newFile(): void {
    const name = prompt('New file name (e.g. hello.ds):', 'untitled.ds');
    if (!name) return;
    const path = resolvePath(this.targetFolder(), name.trim());
    if (this.ws.exists(path)) return this.say(`${name} already exists`);
    this.ws.write(path, path.endsWith('.ds') ? 'print("Hello from Dessa");\n' : '');
    this.expanded.update((s) => new Set([...s, parentOf(path)]));
    this.open(path);
  }

  newFolder(): void {
    const name = prompt('New folder name:');
    if (!name) return;
    const path = resolvePath(this.targetFolder(), name.trim());
    if (this.ws.exists(path)) return this.say(`${name} already exists`);
    this.ws.makeFolder(path);
    this.expanded.update((s) => new Set([...s, parentOf(path), path]));
    this.selected.set(path);
  }

  renameSelected(): void {
    const from = this.selected();
    if (from === '/' || !this.ws.exists(from)) return;
    const name = prompt('Rename to:', nameOf(from));
    if (!name || name === nameOf(from)) return;
    const to = resolvePath(parentOf(from), name.trim());
    if (this.ws.exists(to)) return this.say(`${name} already exists`);
    this.rememberState();
    this.states.clear();
    this.ws.rename(from, to);
    this.selected.set(to);
    this.showActive();
  }

  deleteSelected(): void {
    const path = this.selected();
    if (path === '/' || !this.ws.exists(path)) return;
    if (!confirm(`Delete ${path}${this.ws.isFolder(path) ? ' and everything in it' : ''}?`)) return;
    this.states.delete(path);
    this.ws.remove(path);
    this.selected.set(parentOf(path));
    this.showActive();
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
    if (!confirm('Put back the starter files? Your own files and changes in this playground are deleted.')) return;
    this.states.clear();
    this.ws.reset();
    this.selected.set('/main.ds');
    this.showActive();
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
