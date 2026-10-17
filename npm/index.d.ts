export interface Diagnostic {
    file: string;
    line: number;
    column: number;
    severity: "error" | "warning" | "note";
    message: string;
}

export interface CompileResult {
    ok: boolean;
    js: string;
    diagnostics: Diagnostic[];
}

export interface CompileOptions {
    /** The file name used in messages (default "main.ds"). */
    name?: string;
    /** Other files the program imports, by path: { "lib/util.ds": "..." } */
    files?: Record<string, string>;
}

export class DessaError extends Error {
    diagnostics: Diagnostic[];
}

/** Compiles Dessa source to JavaScript. */
export function compile(source: string, options?: CompileOptions): Promise<CompileResult>;
/** Compiles a .ds file and everything it imports. Throws DessaError on errors. */
export function compileFile(file: string): Promise<string>;
/** Runs Dessa source in this process. Returns the exit code. */
export function run(source: string, options?: CompileOptions): Promise<number>;
/** Runs a .ds file. Returns the exit code. */
export function runFile(file: string): Promise<number>;
/** A .dhtml (or .html with Dessa scripts) page with its scripts compiled: plain HTML. */
export function buildPage(file: string): Promise<string>;
/** The diagnostics of a .ds file or a page. */
export function check(file: string): Promise<Diagnostic[]>;
export function version(): Promise<string>;
export function formatDiagnostic(d: Diagnostic): string;
export function isPage(file: string): boolean;
