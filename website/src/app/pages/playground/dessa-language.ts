// Dessa in CodeMirror: highlighting (the keywords of src/lexer/lexer.cpp),
// and errors, completions and hover from the real compiler (dessa check
// --json, complete, hover), as in the VS Code extension.
import { autocompletion, Completion, CompletionContext, CompletionResult, startCompletion } from '@codemirror/autocomplete';
import { HighlightStyle, StreamLanguage, StringStream, syntaxHighlighting } from '@codemirror/language';
import { Diagnostic as LintDiagnostic, linter } from '@codemirror/lint';
import { EditorState, Extension } from '@codemirror/state';
import { EditorView, hoverTooltip } from '@codemirror/view';
import { tags } from '@lezer/highlight';
import { DessaTyping, inStringOrComment, inTypeBrackets, typeBracketsOf } from './dessa-typing';
import { Diagnostic } from './toolchain';

const KEYWORDS = new Set(
  'var const func class interface enum if else switch case default for while loop break continue return in try catch throw as new this super init with is typeof'.split(' '),
);
const TYPES = new Set('Str Int Bul Flt Dbl Chr Emp Nil Obj Map Func'.split(' '));
const ATOMS = new Set(['true', 'false', 'nil']);

interface State {
  /** what we're inside: a block comment, a triple string, a template string and its {expr} depth */
  in: 'code' | 'comment' | 'triple' | 'template' | 'triple-template';
  /** inside a template's {expr}: how many { are open, and which template to go back to */
  braces: number;
  back: 'template' | 'triple-template' | null;
  /** how many blocks ({ ( [) are open, for the indentation */
  depth: number;
}

function token(stream: StringStream, state: State): string | null {
  if (state.in === 'comment') {
    if (stream.skipTo('*/')) {
      stream.match('*/');
      state.in = 'code';
    } else stream.skipToEnd();
    return 'comment';
  }
  if (state.in === 'triple' || state.in === 'triple-template' || state.in === 'template') {
    const triple = state.in !== 'template';
    const template = state.in !== 'triple';
    while (!stream.eol()) {
      if (triple && stream.match('"""')) {
        state.in = 'code';
        return 'string';
      }
      if (!triple && stream.peek() === '"') {
        stream.next();
        state.in = 'code';
        return 'string';
      }
      if (template && stream.peek() === '{') {
        if (stream.pos > stream.start) return 'string';   // the text before it first
        stream.next();
        state.back = state.in === 'triple-template' ? 'triple-template' : 'template';
        state.in = 'code';
        state.braces = 1;
        return 'meta';
      }
      if (stream.next() === '\\') stream.next();
    }
    if (!triple) state.in = 'code';   // a "..." string ends at the line's end
    return 'string';
  }

  // code (possibly inside a template's {expr})
  if (stream.eatSpace()) return null;
  if (state.back) {
    if (stream.peek() === '{') {
      state.braces++;
    } else if (stream.peek() === '}') {
      state.braces--;
      if (state.braces === 0) {
        stream.next();
        state.in = state.back;
        state.back = null;
        return 'meta';
      }
    }
  }
  if (stream.match('//')) {
    stream.skipToEnd();
    return 'comment';
  }
  if (stream.match('/*')) {
    state.in = 'comment';
    return token(stream, state);
  }
  if (stream.match('$"""')) {
    state.in = 'triple-template';
    return 'string';
  }
  if (stream.match('"""')) {
    state.in = 'triple';
    return 'string';
  }
  if (stream.match('$"')) {
    state.in = 'template';
    return 'string';
  }
  if (stream.match(/^"(?:\\.|[^"\\])*"?/)) return 'string';
  if (stream.match(/^'(?:\\.|[^'\\])+'/)) return 'string';
  if (stream.match(/^(?:0x[0-9a-fA-F_]+|0b[01_]+|0o[0-7_]+|\d[\d_]*(?:\.\d+)?f?)/)) return 'number';
  const word = stream.match(/^[A-Za-z_]\w*/) as RegExpMatchArray | null;
  if (word) {
    const w = word[0];
    if (KEYWORDS.has(w)) return 'keyword';
    if (ATOMS.has(w)) return 'atom';
    if (TYPES.has(w) || /^[A-Z]/.test(w)) return 'typeName';
    if (stream.match(/^\s*\(/, false)) return 'function';
    return 'variableName';
  }
  if (stream.match(/^(?::=|\?\?|\?\.|\|>|==|!=|<=|>=|&&|\|\||\+\+|--|[-+*/%^=<>!?:&|~])/)) return 'operator';
  const c = stream.next();
  if (!state.back && (c === '{' || c === '(' || c === '[')) state.depth++;
  else if (!state.back && (c === '}' || c === ')' || c === ']')) state.depth = Math.max(0, state.depth - 1);
  return null;
}

export const dessaLanguage = StreamLanguage.define<State>({
  name: 'dessa',
  startState: () => ({ in: 'code', braces: 0, back: null, depth: 0 }),
  copyState: (s) => ({ ...s }),
  token,
  // one unit per open block; a line starting with } ) ] closes one
  indent: (state, textAfter, context) => {
    if (state.in === 'comment' || state.in === 'triple' || state.in === 'triple-template') return null;
    return Math.max(0, state.depth - (/^\s*[})\]]/.test(textAfter) ? 1 : 0)) * context.unit;
  },
  languageData: { indentOnInput: /^\s*[}\])]$/, commentTokens: { line: '//', block: { open: '/*', close: '*/' } }, closeBrackets: { brackets: ['(', '[', '{', '"', "'"] } },
  tokenTable: { function: tags.function(tags.variableName), meta: tags.special(tags.brace) },
});

/** The website's code colours (styles.css: --hl-*), for light and dark. */
export const dessaHighlight = syntaxHighlighting(
  HighlightStyle.define([
    { tag: tags.keyword, color: 'var(--hl-keyword)', fontWeight: '600' },
    { tag: tags.typeName, color: 'var(--hl-type)' },
    { tag: tags.string, color: 'var(--hl-string)' },
    { tag: [tags.number, tags.atom, tags.bool], color: 'var(--hl-number)' },
    { tag: tags.comment, color: 'var(--hl-comment)', fontStyle: 'italic' },
    { tag: tags.function(tags.variableName), color: 'var(--hl-function)' },
    { tag: tags.special(tags.brace), color: 'var(--hl-keyword)' },
    { tag: tags.operator, color: 'var(--hl-operator)' },
  ]),
);

/** The editor's colours: CodeMirror's own theme is light, with a black caret. */
export const editorTheme = EditorView.theme({
  '&': { height: '100%', fontSize: '0.9rem', backgroundColor: 'var(--mat-sys-surface)', color: 'var(--mat-sys-on-surface)' },
  '.cm-content': { caretColor: 'var(--mat-sys-on-surface)' },
  '.cm-cursor, .cm-dropCursor': { borderLeft: '2px solid var(--mat-sys-primary)' },
  '.cm-gutters': { backgroundColor: 'var(--mat-sys-surface)', color: 'var(--text-muted)', border: 'none', borderRight: '1px solid var(--line)' },
  '.cm-foldPlaceholder': { backgroundColor: 'var(--code-bg)', border: '1px solid var(--line)', color: 'var(--text-muted)' },
  '.cm-panels': { backgroundColor: 'var(--mat-sys-surface-container-low)', color: 'var(--mat-sys-on-surface)' },
  '.cm-panels-top': { borderBottom: '1px solid var(--line)' },
  '.cm-panels-bottom': { borderTop: '1px solid var(--line)' },
  '.cm-textfield': { backgroundColor: 'var(--mat-sys-surface)', color: 'var(--mat-sys-on-surface)', border: '1px solid var(--line)', borderRadius: '4px' },
  '.cm-button': { backgroundImage: 'none', backgroundColor: 'var(--mat-sys-surface)', color: 'var(--mat-sys-on-surface)', border: '1px solid var(--line)', borderRadius: '4px' },
  '.cm-panel button[name=close]': { color: 'var(--mat-sys-on-surface)' },
  '.cm-searchMatch': { backgroundColor: 'light-dark(#f5e69c, #4d4620)', outline: 'none' },
  '.cm-searchMatch.cm-searchMatch-selected': { backgroundColor: 'light-dark(#e9c94a, #7a6a1e)' },
  '.cm-selectionMatch': { backgroundColor: 'light-dark(#dfeee3, #26342a)' },
  '.cm-matchingBracket, &.cm-focused .cm-matchingBracket': { backgroundColor: 'light-dark(#cdebd5, #2f4a37)', outline: '1px solid var(--line)' },
});

/** What the editor asks the compiler; `path` is the open file's workspace path. */
export interface CompilerHooks {
  path: () => string;
  check: () => Promise<Diagnostic[]>;
  complete: (line: number, column: number) => Promise<string>;
  hover: (line: number, column: number) => Promise<string>;
  /** `dessa symbols` of the open file, or of another workspace file */
  symbols: (file?: string) => Promise<string>;
  /** the other Dessa files an import can name: import spec (lib.util) -> workspace path */
  modules: () => Record<string, string>;
}

const position = (view: EditorView, pos: number) => {
  const line = view.state.doc.lineAt(pos);
  return { line: line.number, column: pos - line.from + 1 };
};

/** Errors and warnings from `dessa check --json`, a moment after typing stops. */
function errors(hooks: CompilerHooks): Extension {
  return linter(
    async (view) => {
      const list = await hooks.check();
      const doc = view.state.doc;
      const here = hooks.path().slice(1);
      const out: LintDiagnostic[] = [];
      for (const d of list) {
        if (d.severity === 'hint' || d.line < 1 || d.line > doc.lines) continue;
        if (d.file && d.file !== here && !d.file.endsWith('/' + here) && d.file !== hooks.path()) continue;
        const line = doc.line(d.line);
        const from = Math.min(line.to, line.from + Math.max(0, d.column - 1));
        const word = /^[\w$]+/.exec(doc.sliceString(from, line.to));
        out.push({
          from,
          to: Math.max(from + (word ? word[0].length : 1), Math.min(line.to, from + 1)),
          severity: d.severity === 'error' ? 'error' : d.severity === 'warning' ? 'warning' : 'info',
          message: d.message,
        });
      }
      return out;
    },
    { delay: 500 },
  );
}

const KIND: Record<string, string> = {
  function: 'function', builtin: 'function', method: 'method', class: 'class', interface: 'interface',
  enum: 'enum', constant: 'constant', field: 'property', module: 'namespace', variable: 'variable', parameter: 'variable',
};

interface Symbol {
  name: string;
  kind: string;
  type?: string;
  params?: { name: string; type: string; default?: string }[];
  doc?: string;
}

const describe = (s: Symbol) => (s.params ? `(${s.params.map((p) => `${p.name}[${p.type}]`).join(', ')})` : (s.type ?? s.kind));

/** The built-in types, as the VS Code extension describes them */
const PRIMITIVES: Record<string, string> = {
  Str: 'Text', Int: '64-bit integer (Int[2], Int[8], Int[16] print in that base)', Bul: 'Boolean (true / false)',
  Flt: '32-bit floating point', Dbl: '64-bit floating point', Chr: 'Single character', Emp: 'No value (functions that return nothing)',
  Nil: 'The type of nil; use T | Nil for optional values',
  Obj: 'Any value (fields are looked up when the program runs): new { name = "Ann" }',
};
const TYPE_KINDS = new Set(['class', 'interface', 'enum']);

interface Symbols {
  globals?: Symbol[];
  libraries?: { name: string; members: Symbol[] }[];
  imports?: { spec: string; file?: string }[];
}

/**
 * `dessa symbols` of the open file, asked again only when its text changed.
 * A half-typed line can stop the compiler: then the last answer stands.
 */
function symbolCache(hooks: CompilerHooks) {
  let text: string | null = null;
  let last: Symbols | null = null;
  let data: Promise<Symbols | null> = Promise.resolve(null);
  return (state: EditorState): Promise<Symbols | null> => {
    const now = state.doc.toString();
    if (now !== text) {
      text = now;
      data = hooks.symbols().then((out) => {
        const parsed = JSON.parse(out) as Symbols;
        if (parsed.libraries?.length || parsed.globals?.length) last = parsed;
        return last;
      }).catch(() => last);
    }
    return data;
  };
}

/** Completions from `dessa complete --at L:C`: names that fit at the cursor, with their types. */
function completions(hooks: CompilerHooks, typing: DessaTyping): Extension {
  const symbols = symbolCache(hooks);
  const option = (s: Symbol, i = 0): Completion => ({ label: s.name, type: KIND[s.kind] ?? 'variable', detail: describe(s), info: s.doc, boost: -i / 1000 });

  // var[|], func[|], name[|] in parameters, class Box:|: the types
  const types = async (context: CompletionContext, owner: string | null): Promise<CompletionResult> => {
    const data = await symbols(context.state);
    const options: Completion[] = Object.entries(PRIMITIVES).map(([label, detail]) => ({
      label, detail, type: 'type', boost: 2,
      apply: (view: EditorView, _c: Completion, from: number, to: number) => {
        view.dispatch({ changes: { from, to, insert: label }, selection: { anchor: from + label.length } });
        typing.leaveType(view, owner);
      },
    }));
    for (const [label, detail] of [['Map', 'Map[K, V]: keys to values'], ['Func', 'Func[Ret, [Params]]: a function']]) {
      options.push({
        label, detail, type: 'type', boost: 2,
        apply: (view, _c, from, to) => {
          view.dispatch({ changes: { from, to, insert: label + '[]' }, selection: { anchor: from + label.length + 1 } });
          startCompletion(view);
        },
      });
    }
    const seen = new Set(options.map((o) => o.label));
    for (const g of data?.globals ?? []) {
      if (!TYPE_KINDS.has(g.kind) || seen.has(g.name)) continue;
      seen.add(g.name);
      options.push({
        label: g.name, detail: g.kind, type: KIND[g.kind],
        apply: (view, _c, from, to) => {
          view.dispatch({ changes: { from, to, insert: g.name }, selection: { anchor: from + g.name.length } });
          typing.leaveType(view, owner);
        },
      });
    }
    return { from: context.matchBefore(/\w*/)!.from, options, validFor: /^\w*$/ };
  };

  return autocompletion({
    activateOnTyping: true,
    override: [
      async (context: CompletionContext): Promise<CompletionResult | null> => {
        const state = context.state;
        const before = state.sliceDoc(Math.max(0, context.pos - 2000), context.pos);
        const lineBefore = before.slice(before.lastIndexOf('\n') + 1);

        // $("dcrypto") { sha| : what the module offers
        const braces = /\$\(\s*"([\w.]+)"(?:\s+as\s+\w+)?\s*\)\s*\{([^{}]*)$/.exec(before);
        if (braces) {
          if (/\bas\s+\w*$/.test(braces[2])) return null;   // naming an alias
          const listed = new Set(braces[2].split(',').map((s) => s.trim().split(/\s+/)[0]).filter(Boolean));
          const data = await symbols(state);
          const names = new Map<string, Symbol>();
          for (const m of data?.libraries?.find((l) => l.name === braces[1])?.members ?? []) names.set(m.name, m);
          const file = data?.imports?.find((i) => i.spec === braces[1] && i.file)?.file;
          const module = file ?? hooks.modules()[braces[1]];
          if (!names.size && module) {
            try {
              for (const g of (JSON.parse(await hooks.symbols(module)) as Symbols).globals ?? []) {
                if (g.kind !== 'builtin' && !g.name.startsWith('_') && !('file' in g)) names.set(g.name, g);
              }
            } catch {
              // not readable right now
            }
          }
          const options = [...names.values()].filter((s) => !listed.has(s.name)).map((s) => option(s));
          return options.length ? { from: context.matchBefore(/\w*/)!.from, options, validFor: /^\w*$/ } : null;
        }

        // $("ma  or  $(ma : the libraries and the workspace's files, with the quotes
        const inImport = /\$\(\s*(?:"[\w.]*"(?:\s+as\s+\w+)?\s*,\s*)*("?)([\w.]*)$/.exec(before);
        if (inImport) {
          const quoted = inImport[1] === '"';
          const typed = inImport[2];
          const data = await symbols(state);
          const make = (name: string, type: string, detail: string): Completion => ({
            label: name, type, detail, boost: type === 'namespace' ? 1 : 0,
            apply: (view, _c, from, to) => {
              const start = from - (quoted ? 1 : 0);
              const end = view.state.sliceDoc(to, to + 1) === '"' ? to + 1 : to;
              const insert = `"${name}"`;
              view.dispatch({ changes: { from: start, to: end, insert }, selection: { anchor: start + insert.length } });
            },
          });
          const options = (data?.libraries ?? []).map((lib) => make(lib.name, 'namespace', `library: ${lib.members.map((m) => m.name).join(', ')}`));
          for (const m of Object.keys(hooks.modules())) options.push(make(m, 'text', 'a file of this workspace'));
          return { from: context.pos - typed.length, options, validFor: /^[\w.]*$/ };
        }
        if (inStringOrComment(state, context.pos)) return null;

        // : after a generic's name (func[T] first:|, class Box:|, new Box:|) or a type's [
        if (/\b(func\s*\[[^\]]*\]\s*\w+|class\s+\w+|new\s+\w+)\s*:\s*\[?[\w,\s]*$/.test(lineBefore)) return types(context, null);
        if (inTypeBrackets(before)) return types(context, typeBracketsOf(before));

        const word = context.matchBefore(/[\w$]*/);
        const dot = state.sliceDoc(Math.max(0, (word?.from ?? context.pos) - 1), word?.from ?? context.pos) === '.';
        if (!context.explicit && !word?.text && !dot) return null;
        const { line, column } = position(context.view!, context.pos);
        let answer: { found?: boolean; items?: Symbol[] } = {};
        try {
          answer = JSON.parse(await hooks.complete(line, column));
        } catch {
          // the fallback below
        }
        if (answer.items?.length) {
          return { from: word?.from ?? context.pos, options: answer.items.map(option), validFor: /^[\w$]*$/ };
        }
        // The compiler couldn't analyse this spot: keywords, types and the file's names
        if (dot) return null;
        const data = await symbols(state);
        const options: Completion[] = [...KEYWORDS].map((k) => ({ label: k, type: 'keyword' }));
        for (const [label, detail] of Object.entries(PRIMITIVES)) options.push({ label, detail, type: 'type' });
        for (const a of ATOMS) options.push({ label: a, type: 'constant' });
        const seen = new Set(options.map((o) => o.label));
        for (const g of data?.globals ?? []) {
          if (seen.has(g.name)) continue;
          seen.add(g.name);
          options.push(option(g));
        }
        return { from: word?.from ?? context.pos, options, validFor: /^[\w$]*$/ };
      },
    ],
  });
}

/** Hovering a name: its declaration from `dessa hover --at L:C`. */
function hovers(hooks: CompilerHooks): Extension {
  return hoverTooltip(async (view, pos) => {
    const { line, column } = position(view, pos);
    let answer: { found?: boolean; type?: string; memberOf?: string; symbol?: Symbol };
    try {
      answer = JSON.parse(await hooks.hover(line, column));
    } catch {
      return null;
    }
    const s = answer.symbol;
    if (!answer.found || !s) return null;
    return {
      pos,
      above: true,
      create: () => {
        const dom = document.createElement('div');
        dom.className = 'pg-hover';
        const head = document.createElement('code');
        const owner = answer.memberOf ? `${answer.memberOf}.` : '';
        head.textContent = s.params ? `${s.kind === 'method' || s.kind === 'function' ? 'func' : s.kind} ${owner}${s.name}${describe(s)}` : `${s.kind} ${owner}${s.name}: ${answer.type ?? s.type ?? ''}`;
        dom.append(head);
        if (s.doc) {
          const doc = document.createElement('p');
          doc.textContent = s.doc;
          dom.append(doc);
        }
        return { dom };
      },
    };
  });
}

export function dessaSupport(hooks: CompilerHooks): Extension[] {
  const typing = new DessaTyping();
  return [dessaLanguage, dessaHighlight, errors(hooks), completions(hooks, typing), hovers(hooks), typing.extension()];
}
