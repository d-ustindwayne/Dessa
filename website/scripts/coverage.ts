// Runs the test suite once per backend and writes data/coverage.json, which the
// features page shows. Build the compiler first. Takes a few minutes.
//     bun scripts/coverage.ts                 run the tests
//     bun scripts/coverage.ts --from <dir>    read cov_default.txt, cov_c.txt, ... saved earlier
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';

const root = join(import.meta.dir, '..', '..');
const modes = [
  { key: 'native', flag: '', file: 'default' },
  { key: 'c', flag: '--c', file: 'c' },
  { key: 'wasm', flag: '--wasm', file: 'wasm' },
];

const from = process.argv.indexOf('--from');

function output(mode: (typeof modes)[number]): string {
  if (from > 0) return readFileSync(join(process.argv[from + 1], `cov_${mode.file}.txt`), 'utf8');
  console.log(`python tests/test.py ${mode.flag}`);
  const args = ['tests/test.py', ...(mode.flag ? [mode.flag] : [])];
  const run = Bun.spawnSync(['python', ...args], { cwd: root });
  return run.stdout.toString() + run.stderr.toString();
}

const cases: Record<string, Record<string, { status: string; reason?: string }>> = {};
for (const mode of modes) {
  for (const line of output(mode).split(/\r?\n/)) {
    const m = /^(ok|FAIL|skip) +((?:run|errors)\/\S+)\.ds(?: \((.*)\))?$/.exec(line);
    if (!m) continue;
    const status = m[1] === 'ok' ? 'pass' : m[1] === 'FAIL' ? 'fail' : 'skip';
    // the runner prints "(union types (Int | Str)", one bracket short
    let reason = m[3];
    if (reason && reason.split('(').length > reason.split(')').length) reason += ')';
    (cases[m[2]] ??= {})[mode.key] = reason ? { status, reason } : { status };
  }
}

mkdirSync(join(import.meta.dir, '..', 'data'), { recursive: true });
const file = join(import.meta.dir, '..', 'data', 'coverage.json');
writeFileSync(file, JSON.stringify({ date: new Date().toLocaleDateString('en-CA'), cases }, null, 1) + '\n');
console.log(`wrote ${file} (${Object.keys(cases).length} tests)`);
