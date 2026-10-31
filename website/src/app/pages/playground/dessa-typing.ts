// Typing helpers, as in the VS Code extension (tools/vscode-dessa/extension.js,
// autoBrackets):
//   $ at the start of a line        -> $(|)
//   var, const, func                -> var[|]        (a type goes there)
//   init in a class, super in init  -> init(|), super(|);
//   space after if, while, for ...  -> if (|)
//   space after loop                -> loop { | } over three lines
//   < where a value starts          -> <|>           (a list); Int< in a type -> Int<>
//   { after an import               -> {  }
//   !! in a type                    -> spaces around it
// Backspace on <|> or an import's { | } removes both sides, and a closing
// bracket typed in front of the same one steps over it.
import { closeCompletion, startCompletion } from '@codemirror/autocomplete';
import { EditorState, Extension, Prec } from '@codemirror/state';
import { EditorView, keymap } from '@codemirror/view';

const CLOSING: Record<string, string> = { '(': ')', '[': ']', '{': '}', '<': '>' };
const UNIT = '    ';

/** Is `pos` inside a string or a comment? The code in a template's {braces} counts as code. */
export function inStringOrComment(state: EditorState, pos: number): boolean {
  const text = state.sliceDoc(0, pos);
  const TRIPLE = '"""';
  type Str = { quote: string; triple: boolean; template: boolean };
  const interpolations: [Str, number][] = [];   // open template strings and their brace depth
  let mode: 'code' | 'line' | 'block' | 'string' = 'code';
  let str: Str = { quote: '"', triple: false, template: false };
  let i = 0;
  while (i < text.length) {
    const c = text[i];
    if (mode === 'line') {
      if (c === '\n') mode = 'code';
      i++;
    } else if (mode === 'block') {
      if (text.startsWith('*/', i)) { mode = 'code'; i += 2; } else i++;
    } else if (mode === 'string') {
      if (c === '\\') { i += 2; continue; }
      if (str.triple && text.startsWith(TRIPLE, i)) { mode = 'code'; i += 3; continue; }
      if (!str.triple && (c === str.quote || c === '\n')) { mode = 'code'; i++; continue; }
      if (str.template && c === '{') { interpolations.push([str, 0]); mode = 'code'; i++; continue; }
      i++;
    } else {
      if (text.startsWith('//', i)) { mode = 'line'; i += 2; }
      else if (text.startsWith('/*', i)) { mode = 'block'; i += 2; }
      else if (text.startsWith('$' + TRIPLE, i)) { mode = 'string'; str = { quote: '"', triple: true, template: true }; i += 4; }
      else if (text.startsWith(TRIPLE, i)) { mode = 'string'; str = { quote: '"', triple: true, template: false }; i += 3; }
      else if (text.startsWith('$"', i)) { mode = 'string'; str = { quote: '"', triple: false, template: true }; i += 2; }
      else if (c === '"' || c === "'") { mode = 'string'; str = { quote: c, triple: false, template: false }; i++; }
      else if (c === '{' && interpolations.length) { interpolations[interpolations.length - 1][1]++; i++; }
      else if (c === '}' && interpolations.length) {
        const top = interpolations[interpolations.length - 1];
        if (top[1] === 0) { interpolations.pop(); mode = 'string'; str = top[0]; } else top[1]--;
        i++;
      } else i++;
    }
  }
  return mode !== 'code';
}

/**
 * The word the type's [ belongs to (var, func, a parameter's name, Map ...), or
 * null when the cursor isn't in a type's [...]. Lists are indexed with <>, so a
 * [ right after a word is always a type. A dotted name (m.Big) is left to
 * member completion.
 */
export function typeBracketsOf(before: string): string | null {
  let depth = 0;
  for (let i = before.length - 1; i >= 0; i--) {
    const c = before[i];
    if (c === ']') depth++;
    else if (c === '[') {
      if (depth > 0) {
        depth--;
        continue;
      }
      const word = /([A-Za-z_]\w*)\s*$/.exec(before.slice(Math.max(0, i - 200), i));
      return word && !/\.\s*\w*$/.test(before.slice(i + 1)) ? word[1] : null;
    } else if (c === ';' || c === '=' || c === '"' || c === '\n') {
      return null;
    }
  }
  return null;
}

export const inTypeBrackets = (before: string) => typeBracketsOf(before) !== null;

/** The text before the { of the block `pos` is in (`class Shop`, `init(x[Int])`, ...) */
function blockHeader(state: EditorState, pos: number): string {
  const text = state.sliceDoc(Math.max(0, pos - 20000), pos);
  let depth = 0;
  for (let i = text.length - 1; i >= 0; i--) {
    if (text[i] === '}') depth++;
    else if (text[i] === '{') {
      if (depth === 0) return text.slice(Math.max(0, i - 300), i).replace(/\s+$/, '');
      depth--;
    }
  }
  return '';
}

/** How many more closing than opening brackets the line has (strings left out) */
function excess(text: string, open: string, close: string): number {
  const code = text.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, '""');
  let n = 0;
  for (const c of code) {
    if (c === open) n--;
    else if (c === close) n++;
  }
  return n;
}

/** The typing helpers of one editor, and what they remember between keys. */
export class DessaTyping {
  /** Brackets just added after var/const/func/init/super, while nothing else was typed */
  private added: { pos: number; kind: 'decl' | 'init' | 'super' | 'cond' } | null = null;
  /** The cursor just left a type's [] after a type was picked: var[Int] | */
  private left: { close: number; cursor: number } | null = null;

  extension(): Extension {
    return [
      Prec.high(EditorView.inputHandler.of((view, from, to, text) => this.typed(view, from, to, text))),
      Prec.high(keymap.of([{ key: 'Backspace', run: (view) => this.backspace(view) }])),
    ];
  }

  /**
   * A type was picked from the suggestions inside var[...], func[...] or a
   * parameter's [...]: the cursor goes past the ], with a space after var,
   * const and func (var[Int] |) and none after a parameter (x[Int]|). Inside
   * Map[...], Func[...] and the like it stays.
   */
  leaveType(view: EditorView, owner: string | null): void {
    if (!owner || !(['var', 'const', 'func'].includes(owner) || /^[a-z_]/.test(owner))) return;
    const space = ['var', 'const', 'func'].includes(owner);
    const at = view.state.selection.main.head;
    if (view.state.sliceDoc(at, at + 1) !== ']') return;
    let target = at + 1;
    if (space) {
      if (view.state.sliceDoc(target, target + 1) === ' ') target++;
      else {
        view.dispatch({ changes: { from: target, insert: ' ' } });
        target++;
      }
    }
    view.dispatch({ selection: { anchor: target } });
    this.left = { close: at, cursor: target };
  }

  private typed(view: EditorView, from: number, to: number, text: string): boolean {
    const added = this.added, left = this.left;
    this.added = null;
    this.left = null;
    const state = view.state;
    if (from !== to || state.selection.ranges.length !== 1 || view.composing) return false;
    const line = state.doc.lineAt(from);
    const before = line.text.slice(0, from - line.from);
    const after = line.text.slice(from - line.from);
    const wide = state.sliceDoc(Math.max(0, from - 2000), from);
    const put = (insert: string, cursor: number, suggest = false, start = from, end = from) => {
      view.dispatch({ changes: { from: start, to: end, insert }, selection: { anchor: start + cursor }, userEvent: 'input.type', scrollIntoView: true });
      if (suggest) startCompletion(view);
      return true;
    };

    if (added && from === added.pos + 1) {
      const pair = state.sliceDoc(added.pos, added.pos + 2);
      // var[x] -> varx: a lowercase letter means a name was meant (variable = 5)
      if (added.kind === 'decl' && /^[a-z_0-9]$/.test(text) && pair === '[]') {
        closeCompletion(view);
        return put(text, 1, false, added.pos, added.pos + 2);
      }
      // super(.) -> super.
      if (added.kind === 'super' && text === '.' && pair === '()') {
        const end = state.sliceDoc(added.pos, added.pos + 3) === '();' ? 3 : 2;
        return put('.', 1, true, added.pos, added.pos + end);
      }
      // The bracket typed out of habit (var[[]]): it is already there
      const open = pair[0];
      if (text === open || text === open + CLOSING[open]) {
        this.added = added;
        return true;
      }
    }

    // var[Int] | then < | : ! go back inside the brackets: var[Int<>] |, var[Int | |]
    if (left && from === left.cursor && ['<', '|', ':', '!'].includes(text) && state.sliceDoc(left.close, left.close + 1) === ']') {
      const between = state.sliceDoc(left.close + 1, from);
      const lead = state.sliceDoc(left.close - 1, left.close) === ' ' ? '' : ' ';
      const inside = ({ '<': '<>', '|': lead + '| ', ':': ':', '!': '!' } as Record<string, string>)[text];
      if (text === '<') {
        put(inside + ']' + between, inside.length + 1 + between.length, false, left.close, from);
        this.left = { close: left.close + 2, cursor: from + 2 };
        return true;
      }
      return put(inside + ']' + between, inside.length, text !== '!', left.close, from);
    }

    // A closing bracket typed in front of the same one: step over it, unless
    // the line needs both (it had all its brackets closed before)
    if ([')', ']', '>'].includes(text) && after.startsWith(text) && !inStringOrComment(state, from)) {
      const open = text === ')' ? '(' : text === ']' ? '[' : '<';
      if (excess(line.text, open, text) === 0) {
        view.dispatch({ selection: { anchor: from + 1 }, scrollIntoView: true });
        return true;
      }
    }

    if (!['$', ' ', '{', '!', 'r', 't', 'c', '<'].includes(text)) return false;
    if (inStringOrComment(state, from)) return false;

    // init (directly in a class) -> init(|); super (in an init) -> super(|);
    if (text === 'r' || text === 't') {
      const call = /(^\s*|[;{}]\s*)(init|super)$/.exec(before + text);
      if (call && after.trim() === '') {
        const header = blockHeader(state, from);
        const fits = call[2] === 'init' ? /\bclass\s+\w[^;{}]*$/.test(header) : /\binit\s*\([^(){};]*\)$/.test(header);
        if (fits) {
          put(text + (call[2] === 'init' ? '()' : '();'), 2);
          this.added = { pos: from + 1, kind: call[2] as 'init' | 'super' };
          return true;
        }
      }
    }
    // `var` typed at the start of a statement -> `var[|]` (not in `var[Int] variable`)
    if (text === 'r' || text === 't' || text === 'c') {
      const word = before + text;
      const start = /(^\s*|[;{}]\s*|\(\s*)(var|const|func)$/.exec(word);
      if (!start || !/^\s*\)?\s*$/.test(after)) return false;
      if (/\b(var|const|func)\b/.test(word.slice(0, word.length - start[2].length))) return false;
      put(text + '[]', 2, true);
      this.added = { pos: from + 1, kind: 'decl' };
      return true;
    }
    // `loop |` -> loop {
    //                 |
    //             }
    if (text === ' ' && /(^|[;{}])\s*loop$/.test(before) && after.trim() === '') {
      const indent = /^\s*/.exec(line.text)![0];
      return put(` {\n${indent}${UNIT}\n${indent}}`, 3 + indent.length + UNIT.length);
    }
    // $ at the start of a line: an import
    if (text === '$' && /^\s*$/.test(before) && after.trim() === '') return put('$()', 2, true);
    // `var |`, `for (var |)`, and `var |x = 5` (typed in front of a name: the space stays)
    if (text === ' ' && /(^|[\s;{(])(var|const|func)$/.test(before) && !/^\s*\[/.test(after)) {
      return put(/^\w/.test(after) ? '[] ' : '[]', 1, true);
    }
    // `if |` -> `if (|)`, the same for while, for, switch and catch
    if (text === ' ' && /(^|[\s;{}])(if|while|for|switch|catch)$/.test(before) && after.trim() === '') {
      put(' ()', 2);
      this.added = { pos: from + 1, kind: 'cond' };   // `if (` typed out of habit stays `if (|)`
      return true;
    }
    // `$("math") {|` -> `$("math") { | }`
    if (text === '{' && /\$\([^()]*\)\s*$/.test(before) && /^\s*$/.test(after)) return put('{  }', 2, true);
    // `x = <|` -> `x = <|>` where a value starts (a list), `var[Int<|]` -> `var[Int<>|]`
    if (text === '<' && !after.startsWith('>')) {
      if (/(^|[^<>!=]=|[(,]|\breturn|:=)\s*$/.test(before) && /^\s*[),;]?\s*$/.test(after)) return put('<>', 1);
      if (/\w$/.test(before) && inTypeBrackets(wide)) return put('<>', 2);
    }
    // `func[Dbl!|` -> `func[Dbl !! |]`: what a function may throw
    if (text === '!' && before.endsWith('!') && !before.endsWith('!!') && inTypeBrackets(wide.slice(0, -1))) {
      const lead = /\s$/.test(before.slice(0, -1)) ? '' : ' ';
      const trail = /^\s/.test(after) ? '' : ' ';
      return put(lead + '!!' + trail, lead.length + 2 + trail.length, true, from - 1, from);
    }
    return false;
  }

  /** <|> and an import's { | } go together; the other pairs are closeBrackets' */
  private backspace(view: EditorView): boolean {
    const state = view.state;
    const sel = state.selection;
    if (sel.ranges.length !== 1 || !sel.main.empty) return false;
    const at = sel.main.head;
    const line = state.doc.lineAt(at);
    const before = line.text.slice(0, at - line.from);
    const after = line.text.slice(at - line.from);
    if (inStringOrComment(state, at)) return false;
    let from = -1, to = -1;
    if (before.endsWith('<') && after.startsWith('>')) [from, to] = [at - 1, at + 1];
    else if (/\$\([^()]*\)\s*\{ $/.test(before) && after.startsWith(' }')) [from, to] = [at - 2, at + 2];
    if (from < 0) return false;
    view.dispatch({ changes: { from, to }, selection: { anchor: from }, userEvent: 'delete.backward', scrollIntoView: true });
    return true;
  }
}
