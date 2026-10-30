// Turns the repository's docs, benchmark results, test coverage and example
// programs into src/generated/content.ts, and writes public/sitemap.xml and
// public/robots.txt. Runs before every build (bun run build).
import { readFileSync, writeFileSync, mkdirSync, existsSync, readdirSync } from 'node:fs';
import { join, posix } from 'node:path';
import { Marked, type Tokens } from 'marked';

export const SITE = 'https://dessa.dustindwayn.ee';
const REPO = 'https://github.com/d-ustindwayne/Dessa';

const web = join(import.meta.dir, '..');
const root = join(web, '..');
const read = (path: string) => readFileSync(join(root, path), 'utf8').replace(/\r\n/g, '\n');

// ---- Dessa highlighting (the keywords come from src/lexer/lexer.cpp)

const KEYWORDS = new Set(
  'var const func class interface enum if else switch case default for while loop break continue return in try catch throw as new this super init with is typeof'.split(' '),
);
const TYPES = new Set('Str Int Bul Flt Dbl Chr Emp Nil Obj Map Func'.split(' '));
const LITERALS = new Set(['true', 'false', 'nil']);

export function escapeHtml(text: string): string {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

const TOKEN =
  /(\/\*[\s\S]*?\*\/|\/\/[^\n]*)|(\$?"""[\s\S]*?"""|\$?"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])+')|(\b(?:0x[0-9a-fA-F_]+|0b[01_]+|0o[0-7_]+|\d[\d_]*(?:\.\d+)?f?)\b)|([A-Za-z_]\w*)/g;

export function highlightDessa(code: string): string {
  let out = '';
  let last = 0;
  for (const m of code.matchAll(TOKEN)) {
    out += escapeHtml(code.slice(last, m.index));
    last = m.index! + m[0].length;
    const text = escapeHtml(m[0]);
    // $"... {expr} ...": the text as a string, each {expr} as code
    if (m[2] && m[0].startsWith('$')) {
      out += highlightTemplate(m[0]);
      continue;
    }
    let kind = '';
    if (m[1]) kind = 'c';
    else if (m[2]) kind = 's';
    else if (m[3]) kind = 'n';
    else if (KEYWORDS.has(m[0])) kind = 'k';
    else if (LITERALS.has(m[0])) kind = 'n';
    else if (TYPES.has(m[0]) || /^[A-Z]/.test(m[0])) kind = 't';
    out += kind ? `<span class="hl-${kind}">${text}</span>` : text;
  }
  return out + escapeHtml(code.slice(last));
}

function highlightTemplate(text: string): string {
  let out = '';
  let piece = '';
  const flush = () => {
    if (piece) out += `<span class="hl-s">${escapeHtml(piece)}</span>`;
    piece = '';
  };
  for (let i = 0; i < text.length; i++) {
    if (text[i] === '{') {
      // the expression up to the matching }
      let depth = 1, j = i + 1;
      while (j < text.length && depth > 0) {
        if (text[j] === '{') depth++;
        else if (text[j] === '}') depth--;
        if (depth > 0) j++;
      }
      flush();
      out += `<span class="hl-p">{</span>${highlightDessa(text.slice(i + 1, j))}${j < text.length ? '<span class="hl-p">}</span>' : ''}`;
      i = j;
    } else {
      piece += text[i];
    }
  }
  flush();
  return out;
}

const highlight = (code: string, lang: string) =>
  lang === 'dessa' || lang === 'ds' ? highlightDessa(code) : escapeHtml(code);

// ---- docs

// The order the docs appear in the sidebar
const DOC_ORDER = ['overview', 'installation', 'language', 'compiler', 'libraries', 'riv', 'web', 'architecture'];

export interface Heading { id: string; text: string }
export interface Doc { slug: string; title: string; description: string; html: string; headings: Heading[] }

function slugify(text: string): string {
  return text.toLowerCase().replace(/<[^>]+>/g, '').replace(/[^\w\s-]/g, '').trim().replace(/\s+/g, '-');
}

function plain(markdown: string): string {
  return markdown
    .replace(/\[([^\]]+)\]\([^)]+\)/g, '$1')
    .replace(/[`*_]/g, '')
    .replace(/\s+/g, ' ')
    .trim();
}

function shorten(text: string, max = 158): string {
  if (text.length <= max) return text;
  const cut = text.slice(0, max);
  return cut.slice(0, cut.lastIndexOf(' ')).replace(/[,;:]$/, '') + '...';
}

/** A link in a doc: other docs stay on the site, everything else in the repository goes to GitHub. */
function rewriteLink(href: string, fromDir: string, slug: string): string {
  if (/^[a-z]+:/i.test(href)) return href;
  if (href.startsWith('#')) return `/docs/${slug}${href}`;
  const [path, hash = ''] = href.split('#');
  const target = posix.normalize(posix.join(fromDir, path));
  const doc = /^docs\/([\w-]+)\.md$/.exec(target);
  if (doc && DOC_ORDER.includes(doc[1])) return `/docs/${doc[1]}${hash ? '#' + hash : ''}`;
  if (target === 'README.md') return '/';
  const kind = /\.[a-z0-9]+$/i.test(target) ? 'blob' : 'tree';
  return `${REPO}/${kind}/main/${target}${hash ? '#' + hash : ''}`;
}

function renderMarkdown(markdown: string, fromDir: string, slug: string) {
  const headings: Heading[] = [];
  const used = new Map<string, number>();
  const marked = new Marked({
    gfm: true,
    renderer: {
      heading(this: any, token: Tokens.Heading) {
        const inner = this.parser.parseInline(token.tokens);
        let id = slugify(token.text);
        const n = used.get(id) ?? 0;
        used.set(id, n + 1);
        if (n) id += '-' + n;
        if (token.depth === 2) headings.push({ id, text: plain(token.text) });
        return `<h${token.depth} id="${id}">${inner}</h${token.depth}>\n`;
      },
      code(token: Tokens.Code) {
        const lang = (token.lang ?? '').trim();
        return `<pre class="code"><code>${highlight(token.text, lang)}</code></pre>\n`;
      },
      link(this: any, token: Tokens.Link) {
        const href = rewriteLink(token.href, fromDir, slug);
        const external = /^https?:/.test(href) ? ' rel="noopener"' : '';
        return `<a href="${escapeHtml(href)}"${external}>${this.parser.parseInline(token.tokens)}</a>`;
      },
    },
  });
  const html = (marked.parse(markdown) as string)
    .replace(/<table>/g, '<div class="table-wrap"><table>')
    .replace(/<\/table>/g, '</table></div>');
  return { html, headings };
}

function firstParagraph(markdown: string): string {
  const blocks = markdown.split(/\n\s*\n/).map((b) => b.trim());
  const para = blocks.find((b) => b && !/^(#|```|\||-|\d+\.|>|<)/.test(b));
  return para ? plain(para) : '';
}

function loadDocs(): Doc[] {
  return DOC_ORDER.filter((slug) => existsSync(join(root, 'docs', slug + '.md'))).map((slug) => {
    const markdown = read(`docs/${slug}.md`);
    const title = plain(/^# (.+)$/m.exec(markdown)?.[1] ?? slug);
    const body = markdown.replace(/^# .+\n/, '');
    const { html, headings } = renderMarkdown(body, 'docs', slug);
    return { slug, title, description: shorten(firstParagraph(body)), html, headings };
  });
}

// ---- benchmark (tests/bench)

function loadBench() {
  const [header, ...lines] = read('tests/bench/results.csv').trim().split('\n');
  const languages = header.split(',').slice(1);
  const tasks = lines.map((line) => {
    const [name, ...ms] = line.split(',');
    return { name, ms: ms.map(Number) };
  });
  const results = read('tests/bench/RESULTS.md');
  const measured = /^Measured (.+?) on (.+?)\.\s*$/m.exec(results);
  const section = (name: string) =>
    (new RegExp(`## ${name}\\n([\\s\\S]*?)(?=\\n## |$)`).exec(results)?.[1] ?? '')
      .split('\n')
      .filter((l) => l.startsWith('- '))
      .map((l) => l.slice(2));
  const about: Record<string, string> = {};
  for (const line of section('The tasks')) {
    const m = /^\*\*(\w+)\*\*: (.+)$/.exec(line);
    if (m) about[m[1]] = m[2];
  }
  const versions = section('Versions').map((l) => {
    const i = l.indexOf(': ');
    return { language: l.slice(0, i), version: l.slice(i + 2) };
  });
  const notes = renderMarkdown(
    (/## Notes\n([\s\S]*)$/.exec(results)?.[1] ?? '').trim(), 'tests/bench', 'overview',
  ).html;
  return {
    date: measured?.[1] ?? '',
    machine: measured?.[2] ?? '',
    languages,
    tasks: tasks.map((t) => ({ ...t, about: about[t.name] ?? '' })),
    versions,
    notes,
  };
}

// ---- test coverage (data/coverage.json, made by scripts/coverage.ts)

function loadCoverage() {
  const data = JSON.parse(readFileSync(join(web, 'data', 'coverage.json'), 'utf8'));
  const features = [];
  let errorTests = 0;
  for (const [name, modes] of Object.entries<any>(data.cases)) {
    if (name.startsWith('errors/')) {
      errorTests++;
      continue;
    }
    const file = `tests/cases/${name}.ds`;
    // the first comment line that isn't a runner instruction ("// exit: 1", "// needs: http")
    const label = read(file)
      .split('\n')
      .map((l) => l.trim())
      .find((l) => l.startsWith('//') && !/^\/\/ (exit|needs):/.test(l))
      ?.replace(/^\/\/\s*/, '')
      .replace(/\s+--\s+/g, ': ');
    features.push({ name: name.replace('run/', ''), label: label || name.replace('run/', ''), file, modes });
  }
  return { date: data.date, errorTests, features };
}

// ---- example programs, with the output the test suite checks

const EXAMPLES = [
  ['hello', 'Hello'],
  ['basics', 'Variables, types and operators'],
  ['nullable', 'Nil safety'],
  ['functions', 'Functions and lambdas'],
  ['classes', 'Classes, interfaces and enums'],
  ['generics', 'Generics'],
  ['maps', 'Maps'],
  ['errors', 'Errors'],
  ['unicode', 'Unicode text'],
  ['operators2', 'Nil operators and the pipe'],
];

function loadExamples() {
  const list = EXAMPLES.map(([name, title]) => ({
    name,
    title,
    file: `tests/cases/run/${name}.ds`,
    code: highlightDessa(read(`tests/cases/run/${name}.ds`).trimEnd()),
    output: escapeHtml(read(`tests/cases/run/${name}.out`).trimEnd()),
  }));
  const page = read('web/example/counter.dhtml');
  return { list, page: escapeHtml(page.trimEnd()) };
}

function readmeSampleSource(): string {
  return /```dessa\n([\s\S]*?)```/.exec(read('README.md'))?.[1] ?? '';
}

function readmeSample(): string {
  return highlightDessa(readmeSampleSource().trimEnd());
}

function readmeFeatures(): string[] {
  const block = /## Features\n\n([\s\S]*?)\n\n/.exec(read('README.md'))?.[1] ?? '';
  return block.split('\n').filter((l) => l.startsWith('- ')).map((l) => {
    const text = escapeHtml(l.slice(2));
    return text.replace(/`([^`]+)`/g, '<code>$1</code>');
  });
}

// ---- write it all

const version = JSON.parse(read('npm/package.json')).version as string;
const docs = loadDocs();
const content = {
  version,
  docs,
  bench: loadBench(),
  coverage: loadCoverage(),
  examples: loadExamples(),
  sample: readmeSample(),
  features: readmeFeatures(),
};

// One file each, so a page only loads what it shows
mkdirSync(join(web, 'src', 'generated'), { recursive: true });
const emit = (file: string, name: string, value: unknown) =>
  writeFileSync(
    join(web, 'src', 'generated', file),
    `// Made by scripts/content.ts from the repository. Do not edit.\nexport const ${name} = ${JSON.stringify(value, null, 1)};\n`,
  );
emit('site.ts', 'SITE_INFO', {
  version,
  sample: content.sample,
  features: content.features,
  docs: docs.map(({ slug, title, description }) => ({ slug, title, description })),
});
emit('docs.ts', 'DOCS', docs);
emit('bench.ts', 'BENCH', content.bench);
emit('coverage.ts', 'COVERAGE', content.coverage);
emit('examples.ts', 'EXAMPLES', content.examples);

// The playground's starter workspace: the README's sample as main.ds, the
// examples (the test suite's programs, so their output is checked), and
// playground/input.ds for input()
const starters: Record<string, string> = {
  '/main.ds': '// Press Run (Ctrl+\\) or type: dessa run main.ds\n\n' + readmeSampleSource(),
};
for (const [name] of EXAMPLES) starters[`/examples/${name}.ds`] = read(`tests/cases/run/${name}.ds`);
// the modules an example imports ($("lib.generic_coll") in generics.ds), next to it as in the tests
for (const name of readdirSync(join(root, 'tests/cases/run/lib'))) {
  if (name.endsWith('.ds') && Object.values(starters).some((s) => s.includes(`$("lib.${name.slice(0, -3)}")`))) {
    starters[`/examples/lib/${name}`] = read(`tests/cases/run/lib/${name}`);
  }
}
starters['/examples/input.ds'] = readFileSync(join(web, 'playground', 'input.ds'), 'utf8').replace(/\r\n/g, '\n');
emit('playground.ts', 'PLAYGROUND', { version, starters });

const today = new Date().toLocaleDateString('en-CA');
const pages = ['/', '/download/', '/benchmarks/', '/features/', '/examples/', '/playground/', '/docs/', ...docs.map((d) => `/docs/${d.slug}/`)];
writeFileSync(
  join(web, 'public', 'sitemap.xml'),
  '<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n' +
    pages
      .map((p) => `  <url><loc>${SITE}${p}</loc><lastmod>${today}</lastmod><priority>${p === '/' ? '1.0' : '0.8'}</priority></url>`)
      .join('\n') +
    '\n</urlset>\n',
);
writeFileSync(join(web, 'public', 'robots.txt'), `User-agent: *\nAllow: /\n\nSitemap: ${SITE}/sitemap.xml\n`);

console.log(`content: ${docs.length} docs, ${content.bench.tasks.length} benchmark tasks, ${content.coverage.features.length} features, version ${version}`);
