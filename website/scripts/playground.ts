// Prepares website/public/playground/ (all of it generated, not in git):
//
//   compiler/     dessa.mjs + dessa.wasm: the dessa command built for WebAssembly
//                 (emcmake cmake -S . -B build-playground && cmake --build build-playground;
//                 this script only checks they're there)
//   objects/      the C runtime and the libraries' C, compiled once for WebAssembly
//                 with the same clang the browser uses, plus objects.json
//   clang/        @yowasp/clang (LLVM in WebAssembly), gzipped; the playground's
//                 worker unpacks it (GitHub Pages doesn't compress .wasm)
//   dessa_wasm.js the loader that runs a program (runtime/dessa_wasm.js)
//   coi-serviceworker.js  the headers that let a program wait for input()
//
//     bun scripts/playground.ts
import { runClang } from '@yowasp/clang';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';
import { gzipSync } from 'node:zlib';

const web = join(import.meta.dir, '..');
const root = join(web, '..');
const out = join(web, 'public', 'playground');
const FLAGS = ['--target=wasm32-wasip1', '-O2', '-fwasm-exceptions', '-mllvm', '-wasm-enable-sjlj'];

if (!existsSync(join(out, 'compiler', 'dessa.mjs'))) {
  console.error('playground: compiler/dessa.mjs is missing; build it first:\n' +
                '  emcmake cmake -S .. -B ../build-playground && cmake --build ../build-playground');
  process.exit(1);
}

// ---- the libraries the playground links: the runtime, then each library's C ----
type Lib = { name: string; dir: string; sources: string[]; cflags: string[]; link: string[] };
const libs: Lib[] = [{ name: 'runtime', dir: 'runtime', sources: ['dessa_rt.c', 'dessa_web.c'], cflags: [], link: [] }];
for (const name of readdirSync(join(root, 'include'))) {   // core libraries: include/<lib>/<lib>.c
  if (existsSync(join(root, 'include', name, `${name}.c`))) {
    libs.push({ name, dir: `include/${name}`, sources: [`${name}.c`], cflags: [], link: [] });
  }
}
const packages = join(root, 'tools', 'riv', 'packages');
for (const name of readdirSync(packages)) {   // riv packages with a `c` line in <name>.native
  const native = join(packages, name, `${name}.native`);
  if (!existsSync(native)) continue;
  const lib: Lib = { name, dir: `tools/riv/packages/${name}`, sources: [], cflags: [], link: [] };
  let impl = '';
  for (const raw of readFileSync(native, 'utf8').split('\n')) {
    const line = raw.replace(/\/\/.*$/, '').trim();
    const [key, ...rest] = line.split(/\s+/);
    const value = rest.join(' ');
    if (key === 'c') impl = value;
    else if (key === 'source' && value.endsWith('.c')) lib.sources.push(value);
    else if (key === 'wasm.cflags') lib.cflags.push(...value.split(/\s+/));
    else if (key === 'wasm.link') lib.link.push(...value.split(/\s+/));
  }
  if (!impl) continue;   // no C version: it can't run in WebAssembly (ashwee, torrent)
  lib.sources.push(impl);
  libs.push(lib);
}

// Headers a source may include: the runtime's and those next to the source
function headers(dir: string): Record<string, string> {
  const files: Record<string, string> = {};
  for (const d of ['runtime', dir]) {
    const walk = (rel: string) => {
      for (const entry of readdirSync(join(root, d, rel), { withFileTypes: true })) {
        const path = rel ? `${rel}/${entry.name}` : entry.name;
        if (entry.isDirectory()) walk(path);
        else if (entry.name.endsWith('.h')) files[path] = readFileSync(join(root, d, path), 'utf8');
      }
    };
    walk('');
  }
  return files;
}

// "sqlite/sqlite3.c" -> { sqlite: { "sqlite3.c": ... } }: clang's file system is a tree
function tree(flat: Record<string, string>): Record<string, unknown> {
  const t: Record<string, any> = {};
  for (const [path, text] of Object.entries(flat)) {
    const parts = path.split('/');
    let at = t;
    for (const p of parts.slice(0, -1)) at = at[p] ??= {};
    at[parts[parts.length - 1]] = text;
  }
  return t;
}

mkdirSync(join(out, 'objects'), { recursive: true });
const manifestPath = join(out, 'objects', 'objects.json');
const old = existsSync(manifestPath) ? JSON.parse(readFileSync(manifestPath, 'utf8')) : { libraries: {} };
const manifest: { flags: string[]; libraries: Record<string, { objects: string[]; link: string[]; hash: string }> } = { flags: FLAGS, libraries: {} };
for (const lib of libs) {
  const files = headers(lib.dir);
  for (const s of lib.sources) files[s] = readFileSync(join(root, lib.dir, s), 'utf8');
  const hash = createHash('sha256').update(JSON.stringify([FLAGS, lib.cflags, files])).digest('hex').slice(0, 16);
  const objects = lib.sources.map((s) => `${lib.name}-${basename(s, '.c')}.o`);
  if (old.libraries[lib.name]?.hash === hash && objects.every((o) => existsSync(join(out, 'objects', o)))) {
    manifest.libraries[lib.name] = old.libraries[lib.name];
    continue;
  }
  const started = Date.now();
  process.stdout.write(`playground: compiling ${lib.name} (${lib.sources.join(', ')})...`);
  let errors = '';
  const decoder = new TextDecoder();
  let result;
  try {
    result = await runClang(['clang', ...FLAGS, ...lib.cflags, '-I.', '-c', ...lib.sources], tree(files),
                            { stderr: (b) => { if (b) errors += decoder.decode(b, { stream: true }); } });
  } catch (e) {
    console.error(`
playground: clang failed on ${lib.name}:
${errors || (e as Error).message}`);
    process.exit(1);
  }
  lib.sources.forEach((s, i) => {
    const object = (result as Record<string, Uint8Array>)[`${basename(s, '.c')}.o`];
    if (!object) throw new Error(`no object for ${s}`);
    writeFileSync(join(out, 'objects', objects[i]), object);
  });
  console.log(` ${((Date.now() - started) / 1000).toFixed(1)} s`);
  manifest.libraries[lib.name] = { objects, link: lib.link, hash };
}
writeFileSync(manifestPath, JSON.stringify(manifest, null, 1));

// ---- clang, gzipped (with the original sizes, for the progress bar) ----
const clangSrc = join(web, 'node_modules', '@yowasp', 'clang', 'gen');
mkdirSync(join(out, 'clang'), { recursive: true });
const sizes: Record<string, number> = {};
for (const name of readdirSync(clangSrc)) {
  const from = join(clangSrc, name);
  if (name === 'bundle.js') {
    copyFileSync(from, join(out, 'clang', name));
    continue;
  }
  const data = readFileSync(from);
  sizes[name] = data.length;
  const target = join(out, 'clang', `${name}.gz`);
  if (!existsSync(target)) writeFileSync(target, gzipSync(data, { level: 9 }));
}
writeFileSync(join(out, 'clang', 'sizes.json'), JSON.stringify(sizes));

// ---- the workers, the program loader and the service worker ----
for (const name of ['toolchain.worker.mjs', 'run.worker.js']) copyFileSync(join(web, 'playground', name), join(out, name));
copyFileSync(join(root, 'runtime', 'dessa_wasm.js'), join(out, 'dessa_wasm.js'));
copyFileSync(join(web, 'node_modules', 'coi-serviceworker', 'coi-serviceworker.min.js'), join(out, 'coi-serviceworker.js'));
console.log(`playground: ready in ${out}`);
