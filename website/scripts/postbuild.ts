// After ng build: GitHub Pages shows 404.html for addresses that don't exist,
// and .nojekyll stops it from skipping files that start with an underscore.
import { copyFileSync, existsSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const out = join(import.meta.dir, '..', 'dist', 'website', 'browser');
const notFound = join(out, '404', 'index.html');
if (!existsSync(notFound)) throw new Error(`missing ${notFound}`);
copyFileSync(notFound, join(out, '404.html'));
writeFileSync(join(out, '.nojekyll'), '');
console.log(`postbuild: ${out} is ready`);
