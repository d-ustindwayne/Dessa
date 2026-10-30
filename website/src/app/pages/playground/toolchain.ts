import { Injectable, signal } from '@angular/core';
import { Content } from './workspace';

export interface CommandResult {
  code: number;
  out: string;
  err: string;
  /** files the command wrote or changed (dessa emit -o, fix-lists) */
  files?: Record<string, Content>;
  /** dessa run / build: the compiled program */
  wasm?: Uint8Array;
}

export interface Diagnostic {
  file: string;
  line: number;
  column: number;
  severity: 'error' | 'warning' | 'note' | 'hint';
  message: string;
}

const BASE = '/playground/';

/**
 * The real Dessa toolchain in Web Workers: the dessa command (compiled to
 * WebAssembly) and clang, in one worker; each program in a worker of its own.
 */
@Injectable()
export class Toolchain {
  readonly compiler = signal<'idle' | 'loading' | 'ready' | 'failed'>('idle');
  readonly clang = signal<{ state: 'idle' | 'loading' | 'ready' | 'failed'; done: number; total: number }>({ state: 'idle', done: 0, total: 0 });
  readonly running = signal(false);
  /** input() works only when the page is cross-origin isolated (SharedArrayBuffer) */
  readonly canInput = typeof crossOriginIsolated !== 'undefined' && crossOriginIsolated;

  private worker: Worker | null = null;
  private nextId = 1;
  private waiting = new Map<number, (r: CommandResult) => void>();

  private tools(): Worker {
    if (this.worker) return this.worker;
    const worker = new Worker(BASE + 'toolchain.worker.mjs', { type: 'module' });
    worker.onmessage = (event) => {
      const data = event.data;
      if (data.type === 'status') {
        if (data.compiler) this.compiler.set(data.compiler);
        if (data.clang) this.clang.set({ state: data.clang, done: data.done ?? 0, total: data.total ?? 0 });
        return;
      }
      const done = this.waiting.get(data.id);
      this.waiting.delete(data.id);
      done?.(data);
    };
    worker.onerror = (event) => {
      this.compiler.set('failed');
      for (const done of this.waiting.values()) done({ code: 1, out: '', err: `The playground's compiler couldn't start: ${event.message || 'unknown error'}\n` });
      this.waiting.clear();
      this.worker = null;
    };
    this.worker = worker;
    return worker;
  }

  private send(message: object): Promise<CommandResult> {
    const id = this.nextId++;
    return new Promise((resolve) => {
      this.waiting.set(id, resolve);
      this.tools().postMessage({ id, ...message });
    });
  }

  /** Starts loading the compiler (a few MB) before the first command needs it. */
  warmUp(files: Record<string, Content>): void {
    void this.send({ type: 'command', args: ['--version'], files: textOnly(files) });
  }

  /** Any dessa command, as on the command line: dessa check main.ds --json */
  command(args: string[], files: Record<string, Content>, cwd = '/'): Promise<CommandResult> {
    return this.send({ type: 'command', args, files: textOnly(files), cwd });
  }

  /** dessa emit --target wasm, then clang: the program as WebAssembly. */
  compile(path: string, files: Record<string, Content>, cwd = '/'): Promise<CommandResult> {
    return this.send({ type: 'compile', path, files: textOnly(files), cwd });
  }

  async diagnostics(path: string, files: Record<string, Content>): Promise<Diagnostic[]> {
    const r = await this.command(['check', path, '--json'], files);
    try {
      return JSON.parse(r.out) as Diagnostic[];
    } catch {
      return [];
    }
  }

  // ---- running a program ----
  private runner: Worker | null = null;
  private input: SharedArrayBuffer | null = null;
  private finish: ((code: number | null) => void) | null = null;

  /**
   * Runs a compiled program. `write` gets its output; `ask` is called when it
   * waits for a line (answer with sendInput). Resolves to the exit code, or
   * null when it was stopped.
   */
  run(wasm: Uint8Array, args: string[], write: (fd: number, text: string) => void, ask: () => void): Promise<number | null> {
    this.stop();
    this.input = this.canInput ? new SharedArrayBuffer(8 + 65536) : null;
    const runner = new Worker(BASE + 'run.worker.js');
    this.runner = runner;
    this.running.set(true);
    return new Promise((resolve) => {
      this.finish = (code) => {
        this.running.set(false);
        this.finish = null;
        resolve(code);
      };
      runner.onmessage = (event) => {
        const m = event.data;
        if (m.type === 'out') write(m.fd, m.text);
        else if (m.type === 'input') ask();
        else if (m.type === 'exit') {
          runner.terminate();
          if (this.runner === runner) this.runner = null;
          this.finish?.(m.code);
        }
      };
      runner.onerror = (event) => {
        write(2, `The program couldn't run: ${event.message}\n`);
        runner.terminate();
        this.runner = null;
        this.finish?.(1);
      };
      runner.postMessage({ wasm, args, input: this.input });
    });
  }

  /** A line typed for input(); null: the end of the input (Ctrl+D). */
  sendInput(line: string | null): void {
    if (!this.input) return;
    const flag = new Int32Array(this.input, 0, 2);
    if (line === null) {
      Atomics.store(flag, 1, -1);
    } else {
      const bytes = new TextEncoder().encode(line).slice(0, 65536);
      new Uint8Array(this.input, 8).set(bytes);
      Atomics.store(flag, 1, bytes.length);
    }
    Atomics.store(flag, 0, 1);
    Atomics.notify(flag, 0);
  }

  /** Ends the running program (Ctrl+C, Stop). */
  stop(): boolean {
    if (!this.runner) return false;
    this.runner.terminate();
    this.runner = null;
    this.finish?.(null);
    return true;
  }

  /** Downloads clang now (the first run would otherwise wait for it). */
  preloadClang(): Promise<CommandResult> {
    return this.send({ type: 'preload-clang' });
  }
}

/** The workers get text files only (a .wasm someone built stays here). */
function textOnly(files: Record<string, Content>): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [p, c] of Object.entries(files)) if (typeof c === 'string') out[p] = c;
  return out;
}
