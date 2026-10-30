// Dessa in CodeMirror: highlighting (the keywords of src/lexer/lexer.cpp),
// and errors, completions and hover from the real compiler (dessa check
// --json, complete, hover), as in the VS Code extension.
import { autocompletion, CompletionContext, CompletionResult } from '@codemirror/autocomplete';
import { HighlightStyle, StreamLanguage, StringStream, syntaxHighlighting } from '@codemirror/language';
import { Diagnostic as LintDiagnostic, linter } from '@codemirror/lint';
import { Extension } from '@codemirror/state';
import { EditorView, hoverTooltip } from '@codemirror/view';
import { tags } from '@lezer/highlight';
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
  stream.next();
  return null;
}

export const dessaLanguage = StreamLanguage.define<State>({
  name: 'dessa',
  startState: () => ({ in: 'code', braces: 0, back: null }),
  copyState: (s) => ({ ...s }),
  token,
  languageData: { commentTokens: { line: '//', block: { open: '/*', close: '*/' } }, closeBrackets: { brackets: ['(', '[', '{', '"', "'"] } },
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

/** What the editor asks the compiler; `path` is the open file's workspace path. */
export interface CompilerHooks {
  path: () => string;
  check: () => Promise<Diagnostic[]>;
  complete: (line: number, column: number) => Promise<string>;
  hover: (line: number, column: number) => Promise<string>;
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

/** Completions from `dessa complete --at L:C`: names that fit at the cursor, with their types. */
function completions(hooks: CompilerHooks): Extension {
  return autocompletion({
    activateOnTyping: true,
    override: [
      async (context: CompletionContext): Promise<CompletionResult | null> => {
        const word = context.matchBefore(/[\w$]*/);
        const before = context.state.sliceDoc(Math.max(0, (word?.from ?? context.pos) - 1), word?.from ?? context.pos);
        if (!context.explicit && !word?.text && before !== '.') return null;
        const { line, column } = position(context.view!, context.pos);
        let answer: { found?: boolean; items?: Symbol[] };
        try {
          answer = JSON.parse(await hooks.complete(line, column));
        } catch {
          return null;
        }
        if (!answer.items?.length) return null;
        return {
          from: word?.from ?? context.pos,
          options: answer.items.map((s, i) => ({
            label: s.name,
            type: KIND[s.kind] ?? 'variable',
            detail: describe(s),
            info: s.doc,
            boost: -i / 1000,
          })),
          validFor: /^[\w$]*$/,
        };
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
  return [dessaLanguage, dessaHighlight, errors(hooks), completions(hooks), hovers(hooks)];
}
