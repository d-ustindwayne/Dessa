export interface Diagnostic {
    file: string;
    line: number;
    column: number;
    severity: "error" | "warning" | "note" | "hint";
    message: string;
}

export class DessaError extends Error {
    diagnostics: Diagnostic[];
}

/**
 * The path of the `dessa` this package uses. The first time, it downloads
 * Dessa and the WebAssembly compiler into ~/.dessa/npm/<version>.
 */
export function install(): Promise<string>;
/** The errors and warnings of a .ds file or a page. */
export function check(file: string): Promise<Diagnostic[]>;
/**
 * Compiles a .ds file to WebAssembly (app.wasm, and app.js next to it to run it
 * with Node). Resolves to the .wasm file's path.
 */
export function build(file: string, options?: { out?: string }): Promise<string>;
/** A .dhtml page (or .html with Dessa scripts) with its scripts compiled to WebAssembly. */
export function buildPage(file: string): Promise<string>;
/** Runs a .ds file as WebAssembly in Node. Resolves to its exit code. */
export function run(file: string, args?: string[]): Promise<number>;
/** The Dessa version this package drives. */
export function version(): string;
export function isPage(file: string): boolean;
/** Runs dessa itself with these arguments. */
export function dessa(args: string[], options?: { inherit?: boolean; cwd?: string }): Promise<{ code: number; stdout: string; stderr: string }>;
