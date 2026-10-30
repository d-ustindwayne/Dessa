// The playground's terminal: a line editor on xterm.js and the commands it
// understands. `dessa ...` runs the real compiler (built for WebAssembly) on
// the workspace; `dessa run` / `dessa build` then compile its C with clang in
// the browser. The other commands (ls, cat, mkdir, ...) only work on the
// workspace kept in this browser: there is no system shell here.
import type { Terminal } from '@xterm/xterm';
import { Toolchain } from './toolchain';
import { Workspace, nameOf, resolvePath } from './workspace';

const GREY = '\x1b[90m', RED = '\x1b[31m', YELLOW = '\x1b[33m', GREEN = '\x1b[32m', BOLD = '\x1b[1m', RESET = '\x1b[0m';

const HELP = `${BOLD}The Dessa playground's terminal${RESET}

  dessa run <file.ds> [-- args]     compile the program to WebAssembly and run it
  dessa build <file.ds> [-o x.wasm] compile it: x.wasm, and x.js to run it with Node
  dessa check <file.ds> [--json]    errors and warnings only
  dessa emit <file.ds> [-o out.c]   the C Dessa makes (--target wasm: as for WebAssembly)
  dessa tokens | ast <file.ds>      what the lexer and parser read
  dessa complete | hover <file.ds> --at L:C   what the editor asks
  dessa --version, dessa help       the compiler's own version and help

Workspace (the files in this browser; not a system shell):
  ls [dir]  cd <dir>  pwd  cat <file>  mkdir <dir>  touch <file>  rm <path>  mv <from> <to>
  clear     history   help

Keys: Up/Down history, Tab completes names, Ctrl+C stops a program, Ctrl+L clears.
Every dessa command is the real compiler, built for WebAssembly.`;

export class Shell {
  private line = '';
  private cursor = 0;
  private history: string[] = [];
  private back = -1;   // position while going through the history
  private busy = false;
  private awaitingInput = false;
  cwd = '/';

  constructor(
    private term: Terminal,
    private ws: Workspace,
    private tools: Toolchain,
    private onFilesChanged: () => void,
  ) {
    try {
      this.history = JSON.parse(sessionStorage.getItem('dessa-playground-history') ?? '[]');
    } catch {
      this.history = [];
    }
    term.onData((data) => this.key(data));
  }

  start(): void {
    this.term.writeln(`${BOLD}Dessa playground${RESET} ${GREY}(type help for the commands; the Run button runs the open file)${RESET}`);
    this.prompt();
  }

  get isBusy(): boolean {
    return this.busy;
  }

  private prompt(): void {
    this.line = '';
    this.cursor = 0;
    this.term.write(`${GREEN}${this.cwd === '/' ? '~' : '~' + this.cwd}${RESET} $ `);
  }

  /** Prints text from a command or program (\n becomes \r\n for the terminal). */
  print(text: string, color = ''): void {
    const t = text.replace(/\r?\n/g, '\r\n');
    this.term.write(color ? color + t + RESET : t);
  }

  /** Runs a command as if it was typed (the Run button). */
  async type(command: string): Promise<void> {
    if (this.busy) return;
    this.term.write('\x1b[2K\r');   // the prompt line again, with the command on it
    this.prompt();
    this.term.write(command);
    this.line = command;
    this.cursor = command.length;
    await this.enter();
  }

  clear(): void {
    this.term.clear();
  }

  // ---- keys ----
  private key(data: string): void {
    if (data === '\x03') {   // Ctrl+C
      if (this.tools.stop()) {
        this.term.write('^C');
        return;   // the run's end prints the prompt
      }
      if (!this.busy) {
        this.term.write('^C\r\n');
        this.prompt();
      }
      return;
    }
    if (data === '\x0c') {   // Ctrl+L
      this.term.clear();
      return;
    }
    if (this.busy && !this.awaitingInput) return;
    if (data === '\x04' && this.awaitingInput) {   // Ctrl+D: the end of the input
      this.awaitingInput = false;
      this.term.write('\r\n');
      this.tools.sendInput(null);
      return;
    }
    if (data === '\r') {
      void this.enter();
      return;
    }
    if (data === '\x7f' || data === '\b') {
      if (this.cursor > 0) {
        this.line = this.line.slice(0, this.cursor - 1) + this.line.slice(this.cursor);
        this.cursor--;
        this.redraw();
      }
      return;
    }
    if (data === '\t' && !this.awaitingInput) {
      this.complete();
      return;
    }
    const moves: Record<string, () => void> = {
      '\x1b[D': () => (this.cursor = Math.max(0, this.cursor - 1)),
      '\x1b[C': () => (this.cursor = Math.min(this.line.length, this.cursor + 1)),
      '\x1b[H': () => (this.cursor = 0),
      '\x1b[F': () => (this.cursor = this.line.length),
      '\x1b[3~': () => (this.line = this.line.slice(0, this.cursor) + this.line.slice(this.cursor + 1)),
      '\x1b[A': () => this.recall(1),
      '\x1b[B': () => this.recall(-1),
    };
    if (moves[data]) {
      if (this.awaitingInput && (data === '\x1b[A' || data === '\x1b[B')) return;
      moves[data]();
      this.redraw();
      return;
    }
    if (data.startsWith('\x1b')) return;
    // typed or pasted text (only the first line of a paste)
    const text = data.replace(/[\r\n].*$/s, '').replace(/[\x00-\x1f]/g, '');
    this.line = this.line.slice(0, this.cursor) + text + this.line.slice(this.cursor);
    this.cursor += text.length;
    this.redraw();
    if (/[\r\n]/.test(data)) void this.enter();
  }

  private recall(step: number): void {
    if (!this.history.length) return;
    this.back = Math.max(-1, Math.min(this.history.length - 1, this.back + step));
    this.line = this.back < 0 ? '' : this.history[this.history.length - 1 - this.back];
    this.cursor = this.line.length;
  }

  /** Redraws the line being typed, with the cursor in place. */
  private redraw(): void {
    this.term.write('\x1b[2K\r');
    if (this.awaitingInput) this.term.write(this.pendingPrompt);
    else this.term.write(`${GREEN}${this.cwd === '/' ? '~' : '~' + this.cwd}${RESET} $ `);
    this.term.write(this.line);
    const left = this.line.length - this.cursor;
    if (left > 0) this.term.write(`\x1b[${left}D`);
  }

  private pendingPrompt = '';

  /** Tab: completes the last word as a file or folder name. */
  private complete(): void {
    const at = this.line.lastIndexOf(' ', this.cursor - 1) + 1;
    const word = this.line.slice(at, this.cursor);
    const dirPart = word.includes('/') ? word.slice(0, word.lastIndexOf('/') + 1) : '';
    const dir = resolvePath(this.cwd, dirPart || '.');
    const names = this.children(dir).filter((n) => n.startsWith(word.slice(dirPart.length)));
    if (names.length === 1) {
      const done = dirPart + names[0] + (this.ws.isFolder(resolvePath(dir, names[0])) ? '/' : ' ');
      this.line = this.line.slice(0, at) + done + this.line.slice(this.cursor);
      this.cursor = at + done.length;
      this.redraw();
    } else if (names.length > 1) {
      this.term.write('\r\n' + names.join('  ') + '\r\n');
      this.redraw();
    }
  }

  private children(dir: string): string[] {
    const set = new Set<string>();
    const prefix = dir === '/' ? '/' : dir + '/';
    for (const p of [...Object.keys(this.ws.files()), ...this.ws.allFolders()]) {
      if (p !== dir && p.startsWith(prefix)) set.add(p.slice(prefix.length).split('/')[0]);
    }
    return [...set].sort();
  }

  private async enter(): Promise<void> {
    const line = this.line;
    this.term.write('\r\n');
    if (this.awaitingInput) {
      this.awaitingInput = false;
      this.tools.sendInput(line + '\n');
      return;
    }
    this.back = -1;
    if (line.trim()) {
      if (this.history[this.history.length - 1] !== line) this.history.push(line);
      this.history = this.history.slice(-200);
      try {
        sessionStorage.setItem('dessa-playground-history', JSON.stringify(this.history));
      } catch {
        // storage off: the history lasts this visit
      }
      this.busy = true;
      try {
        await this.execute(line.trim());
      } catch (e) {
        this.print(`${(e as Error).message}\n`, RED);
      }
      this.busy = false;
    }
    this.prompt();
  }

  // ---- commands ----
  private async execute(line: string): Promise<void> {
    const words = splitWords(line);
    const [command, ...args] = words;
    const path = (p: string) => resolvePath(this.cwd, p);
    switch (command) {
      case 'help':
        this.print(HELP + '\n');
        return;
      case 'clear':
        this.term.clear();
        return;
      case 'history':
        this.history.forEach((h, i) => this.print(`${String(i + 1).padStart(4)}  ${h}\n`));
        return;
      case 'pwd':
        this.print(this.cwd + '\n');
        return;
      case 'cd': {
        const target = path(args[0] ?? '/');
        if (!this.ws.isFolder(target)) throw new Error(`cd: no such folder: ${args[0]}`);
        this.cwd = target;
        return;
      }
      case 'ls': {
        const dir = path(args[0] ?? '.');
        if (!this.ws.isFolder(dir)) {
          if (this.ws.exists(dir)) this.print(nameOf(dir) + '\n');
          else throw new Error(`ls: no such file or folder: ${args[0]}`);
          return;
        }
        const names = this.children(dir).map((n) => (this.ws.isFolder(resolvePath(dir, n)) ? `${BOLD}${n}/${RESET}` : n));
        if (names.length) this.print(names.join('  ') + '\n');
        return;
      }
      case 'cat': {
        if (!args.length) throw new Error('cat: which file?');
        for (const a of args) {
          const p = path(a);
          if (!(p in this.ws.files())) throw new Error(`cat: no such file: ${a}`);
          if (!this.ws.isText(p)) throw new Error(`cat: ${a} isn't text (it's a WebAssembly program)`);
          const text = this.ws.text(p);
          this.print(text.endsWith('\n') || !text ? text : text + '\n');
        }
        return;
      }
      case 'mkdir':
        if (!args.length) throw new Error('mkdir: which folder?');
        for (const a of args.filter((x) => x !== '-p')) this.ws.makeFolder(path(a));
        this.onFilesChanged();
        return;
      case 'touch':
        if (!args.length) throw new Error('touch: which file?');
        for (const a of args) if (!(path(a) in this.ws.files())) this.ws.write(path(a), '');
        this.onFilesChanged();
        return;
      case 'rm': {
        const targets = args.filter((x) => !x.startsWith('-'));
        if (!targets.length) throw new Error('rm: which file?');
        for (const a of targets) {
          if (!this.ws.exists(path(a)) || path(a) === '/') throw new Error(`rm: no such file or folder: ${a}`);
          this.ws.remove(path(a));
        }
        this.onFilesChanged();
        return;
      }
      case 'mv': {
        if (args.length !== 2) throw new Error('mv: mv <from> <to>');
        const from = path(args[0]);
        let to = path(args[1]);
        if (!this.ws.exists(from)) throw new Error(`mv: no such file or folder: ${args[0]}`);
        if (this.ws.isFolder(to)) to = resolvePath(to, nameOf(from));
        if (this.ws.exists(to)) throw new Error(`mv: ${args[1]} already exists`);
        this.ws.rename(from, to);
        this.onFilesChanged();
        return;
      }
      case 'dessa':
        await this.dessa(args);
        return;
      case 'riv':
        throw new Error('riv isn\'t in the playground: it downloads and shares packages over the network. The bundled packages (json, sql, request, numbers, codes, colors, textfx) can be imported directly.');
      case 'node':
        throw new Error('There is no Node here. dessa run compiles a program and runs it in this browser.');
      default:
        throw new Error(`${command}: not a command here (type help)`);
    }
  }

  private async dessa(args: string[]): Promise<void> {
    const sub = args[0];
    if (sub === 'run' || sub === 'build') return this.build(sub, args.slice(1));
    if (sub === 'setup' || sub === 'serve' || sub === 'home') {
      throw new Error(`dessa ${sub} works on your own computer only (the playground already has what it needs).`);
    }
    const r = await this.tools.command(args, this.ws.files(), this.cwd);
    this.output(r.out, r.err);
    if (r.files && Object.keys(r.files).length) {
      for (const [p, c] of Object.entries(r.files)) this.ws.write(p, c);
      this.onFilesChanged();
    }
    if (r.code !== 0 && !r.out && !r.err) this.print(`exit code ${r.code}\n`, GREY);
  }

  /** dessa run / dessa build: the C from dessa emit --target wasm, compiled by clang here. */
  private async build(sub: 'run' | 'build', args: string[]): Promise<void> {
    const dash = args.indexOf('--');
    const programArgs = dash >= 0 ? args.slice(dash + 1) : [];
    const options = dash >= 0 ? args.slice(0, dash) : args;
    let output = '';
    const files: string[] = [];
    for (let i = 0; i < options.length; i++) {
      if (options[i] === '-o') output = options[++i] ?? '';
      else if (options[i] === '--target' || options[i] === '--port') i++;   // the playground always makes WebAssembly
      else if (!options[i].startsWith('-')) files.push(options[i]);
    }
    if (sub === 'run' && dash < 0 && files.length > 1) programArgs.push(...files.splice(1));
    const file = files[0];
    if (!file) throw new Error(`dessa ${sub}: missing <file.ds>`);
    const target = resolvePath(this.cwd, file);
    if (!(target in this.ws.files())) throw new Error(`dessa: file not found: ${file}`);
    if (isPage(file)) {
      throw new Error('Pages (.dhtml) open in a browser tab with dessa run on your own computer; the playground runs programs (.ds). dessa check works on pages here.');
    }
    if (this.tools.clang().state !== 'ready') this.print('Getting the C compiler ready (downloaded once, about 27 MB; your browser keeps it)...\n', GREY);
    const r = await this.tools.compile(file, this.ws.files(), this.cwd);
    this.output(r.out, r.err);
    if (r.code !== 0 || !r.wasm) return;
    if (sub === 'build') {
      const wasmPath = resolvePath(this.cwd, output || file.replace(/\.[^./]+$/, '') + '.wasm');
      this.ws.write(wasmPath, r.wasm);
      // and the .js that runs it with Node, as dessa build --target wasm makes it
      const loader = await (await fetch('/playground/dessa_wasm.js')).text();
      const js = wasmPath.replace(/\.wasm$/, '.js');
      this.ws.write(js, `${loader}\nprocess.exitCode = DessaWasm.main(require("path").join(__dirname, ${JSON.stringify(nameOf(wasmPath))}), process.argv.slice(2));\n`);
      this.onFilesChanged();
      return;
    }
    const code = await this.tools.run(
      r.wasm,
      programArgs,
      (fd, text) => this.print(text, fd === 2 ? RED : ''),
      () => {
        this.awaitingInput = true;
        this.pendingPrompt = this.currentLineOfOutput();
        this.line = '';
        this.cursor = 0;
        if (!this.tools.canInput) {
          this.awaitingInput = false;
          this.print('\n(input() needs this page to reload once to allow it; reload the page)\n', GREY);
        }
      },
    );
    this.awaitingInput = false;
    if (code === null) this.print('\nstopped\n', GREY);
    else if (code !== 0) this.print(`exit code ${code}\n`, GREY);
  }

  /** What the program printed on the current line (the prompt of an input()). */
  private currentLineOfOutput(): string {
    const buffer = this.term.buffer.active;
    const row = buffer.getLine(buffer.cursorY + buffer.viewportY);
    return row ? row.translateToString(true).slice(0, buffer.cursorX) : '';
  }

  /** The compiler's output, errors in red and warnings in yellow. */
  private output(out: string, err: string): void {
    if (out) this.print(out);
    for (const line of err.split('\n')) {
      if (!line) continue;
      const color = /\berror\b/.test(line) ? RED : /\bwarning\b/.test(line) ? YELLOW : '';
      this.print(line + '\n', color);
    }
  }
}

function isPage(file: string): boolean {
  return /\.(dhtml|html?)$/i.test(file);
}

/** Splits a command line into words; "quoted words" stay together. */
function splitWords(line: string): string[] {
  const words: string[] = [];
  const pattern = /"([^"]*)"|'([^']*)'|(\S+)/g;
  let m;
  while ((m = pattern.exec(line))) words.push(m[1] ?? m[2] ?? m[3]);
  return words;
}

