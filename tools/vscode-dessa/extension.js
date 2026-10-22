// Dessa for VS Code.
//
// Everything language-aware comes from the compiler itself:
//   dessa check    <file> --json --stdin       -> diagnostics (squiggles)
//   dessa symbols  <file> --stdin              -> globals, classes, enums, libraries
//   dessa complete <file> --stdin --at L:C     -> completion at the cursor
//   dessa hover    <file> --stdin --at L:C     -> hover / signature help
// The unsaved buffer is piped through stdin, so errors update as you type.
'use strict';

const vscode = require('vscode');
const cp = require('child_process');
const path = require('path');
const fs = require('fs');

const PRIMITIVES = {
    Str: 'Text', Int: '64-bit integer (Int[2], Int[8], Int[16] print in that base)', Bul: 'Boolean (true / false)',
    Flt: '32-bit floating point', Dbl: '64-bit floating point', Chr: 'Single character', Emp: 'No value (functions that return nothing)',
    Nil: 'The type of nil; use T | Nil for optional values',
    Obj: 'Any value (fields are looked up when the program runs): new { name = "Ann" }',
};

// Builtin methods, mirroring Analyser::builtinMethod.
const METHODS = {
    Str: [
        ['length', '() := Int'], ['isEmpty', '() := Bul'], ['upper', '() := Str'], ['lower', '() := Str'], ['trim', '() := Str'],
        ['includes', '(text[Str]) := Bul'], ['startsWith', '(text[Str]) := Bul'], ['endsWith', '(text[Str]) := Bul'],
        ['indexOf', '(text[Str]) := Int'], ['replace', '(target[Str], replacement[Str]) := Str'], ['replaceAll', '(target[Str], replacement[Str]) := Str'], ['split', '(separator[Str]) := Str<>'],
        ['substr', '(start[Int], length[Int]) := Str'], ['repeat', '(count[Int]) := Str'], ['chars', '() := Chr<>'],
        ['toInt', '() := Int'], ['toDbl', '() := Dbl'], ['toFlt', '() := Flt'], ['toStr', '() := Str'],
    ],
    Chr: [
        ['isDigit', '() := Bul'], ['isAlpha', '() := Bul'], ['isSpace', '() := Bul'], ['isUpper', '() := Bul'],
        ['isLower', '() := Bul'], ['upper', '() := Chr'], ['lower', '() := Chr'], ['code', '() := Int'], ['toStr', '() := Str'],
    ],
    Num: [['toStr', '() := Str'], ['toInt', '() := Int'], ['toDbl', '() := Dbl'], ['toFlt', '() := Flt'], ['toChr', '() := Chr (Int only)'], ['toFixed', '(digits[Int]) := Str (Flt/Dbl only)']],
    Bul: [['toStr', '() := Str']],
    Enum: [['name', '() := Str'], ['value', '() := Int'], ['toStr', '() := Str']],
    Collection: [
        ['length', '() := Int'], ['isEmpty', '() := Bul'], ['includes', '(value[T]) := Bul'], ['clear', '() := Emp'],
        ['forEach', '(action[(T) := Emp]) := Emp'], ['filter', '(keep[(T) := Bul]) := Same'], ['map', '(transform[(T) := U]) := U<>'],
        ['toList', '() := T<>'],
    ],
    List: [
        ['push', '(value[T]) := Emp'], ['pop', '() := T'], ['first', '() := T'], ['last', '() := T'],
        ['insert', '(index[Int], value[T]) := Emp'], ['removeAt', '(index[Int]) := T'], ['remove', '(value[T]) := Bul'],
        ['indexOf', '(value[T]) := Int'], ['reverse', '() := Emp'], ['sort', '() := Emp'], ['slice', '(start[Int], end[Int]) := T<>'],
        ['copy', '() := T<>'], ['join', '(separator[Str]) := Str'],
    ],
    Set: [['add', '(value[T]) := Emp'], ['remove', '(value[T]) := Bul']],
    Map: [
        ['length', '() := Int'], ['isEmpty', '() := Bul'], ['has', '(key[K]) := Bul'], ['get', '(key[K]) := V | Nil'],
        ['getOr', '(key[K], fallback[V]) := V'], ['set', '(key[K], value[V]) := Emp'], ['remove', '(key[K]) := Bul'],
        ['keys', '() := K<>'], ['values', '() := V<>'], ['clear', '() := Emp'], ['copy', '() := Map[K, V]'],
        ['forEach', '(action[Func[Emp, [K, V]]]) := Emp'],
    ],
};

let diagnosticCollection;
let output;
let warnedMissingCompiler = false;
const timers = new Map();
const reportedFiles = new Map();   // document uri -> uris it put diagnostics on
const symbolCache = new Map();     // document uri -> { version, data }

// ============================================================
// Running the compiler
// ============================================================

function compilerPath() {
    return vscode.workspace.getConfiguration('dessa').get('compilerPath') || 'dessa';
}

// Extra folders where `$("lib")` finds riv packages (besides riv_modules):
// the dessa.packageFolders setting, plus tools/riv/packages when the Dessa
// repository itself is open.
function compilerEnv() {
    const folders = [...(vscode.workspace.getConfiguration('dessa').get('packageFolders') || [])];
    for (const ws of vscode.workspace.workspaceFolders || []) {
        const bundled = path.join(ws.uri.fsPath, 'tools', 'riv', 'packages');
        if (fs.existsSync(bundled)) folders.push(bundled);
    }
    if (process.env.DESSA_PACKAGES) folders.push(process.env.DESSA_PACKAGES);
    return { ...process.env, DESSA_PACKAGES: folders.join(path.delimiter) };
}

function runDessa(args, input, cwd) {
    return new Promise((resolve) => {
        let child;
        try {
            child = cp.spawn(compilerPath(), args, { cwd, windowsHide: true, env: compilerEnv() });
        } catch (err) {
            resolve({ code: -1, stdout: '', stderr: String(err), missing: true });
            return;
        }
        let stdout = '';
        let stderr = '';
        child.stdout.on('data', (d) => (stdout += d));
        child.stderr.on('data', (d) => (stderr += d));
        child.on('error', (err) => resolve({ code: -1, stdout, stderr: String(err), missing: err.code === 'ENOENT' }));
        child.on('close', (code) => resolve({ code, stdout, stderr }));
        if (input !== undefined) {
            child.stdin.on('error', () => {});
            child.stdin.end(input);
        }
    });
}

function warnMissing() {
    if (warnedMissingCompiler) return;
    warnedMissingCompiler = true;
    vscode.window
        .showWarningMessage(
            `Dessa compiler not found ("${compilerPath()}"). Set "dessa.compilerPath" to your dessa executable.`,
            'Open Settings'
        )
        .then((choice) => {
            if (choice) vscode.commands.executeCommand('workbench.action.openSettings', 'dessa.compilerPath');
        });
}

const DESSA_SCRIPT = /<script\b[^>]*\btype\s*=\s*["']?(?:text|application)\/(?:dessa|ds|x-dessa)\b/i;

// A Dessa file, a .dhtml page, or an HTML page with <script type="text/dessa">
function isDessa(doc) {
    if (!doc || doc.uri.scheme !== 'file') return false;
    if (doc.languageId === 'dessa' || doc.languageId === 'dessa-html') return true;
    return doc.languageId === 'html' && DESSA_SCRIPT.test(doc.getText());
}

function isPage(doc) {
    return doc.languageId === 'dessa-html' || doc.languageId === 'html';
}

// In a page: is the position inside a Dessa <script>? (.dhtml: a script
// without a type is Dessa too). Plain .ds files always are.
function inDessaScript(doc, position) {
    if (!isPage(doc)) return true;
    const before = doc.getText(new vscode.Range(new vscode.Position(0, 0), position)).replace(/<!--[\s\S]*?-->/g, '');
    const open = before.lastIndexOf('<script');
    if (open < 0 || before.indexOf('</script', open) >= 0) return false;
    const tagEnd = before.indexOf('>', open);
    if (tagEnd < 0) return false;
    const tag = before.slice(open, tagEnd + 1);
    if (DESSA_SCRIPT.test(tag)) return true;
    return doc.languageId === 'dessa-html' && !/\btype\s*=/i.test(tag) && !/\bsrc\s*=/i.test(tag);
}

// Is the position inside a string or a comment rather than code? The code
// inside a template string's {braces} counts as code.
function inStringOrComment(doc, position) {
    const start = doc.languageId === 'dessa' ? 0 : doc.offsetAt(new vscode.Position(position.line, 0));
    const text = doc.getText().slice(start, doc.offsetAt(position));
    const TRIPLE = '"'.repeat(3);
    const interpolations = [];   // open template strings: [string state, brace depth]
    let state = 'code', str = null, i = 0;
    while (i < text.length) {
        const c = text[i];
        if (state === 'line') {
            if (c === '\n') state = 'code';
            i++;
        } else if (state === 'block') {
            if (text.startsWith('*/', i)) { state = 'code'; i += 2; } else i++;
        } else if (state === 'string') {
            if (c === '\\') { i += 2; continue; }
            if (str.triple && text.startsWith(TRIPLE, i)) { state = 'code'; i += 3; continue; }
            if (!str.triple && (c === str.quote || c === '\n')) { state = 'code'; i++; continue; }
            if (str.template && c === '{') { interpolations.push([str, 0]); state = 'code'; i++; continue; }
            i++;
        } else {
            if (text.startsWith('//', i)) { state = 'line'; i += 2; }
            else if (text.startsWith('/*', i)) { state = 'block'; i += 2; }
            else if (text.startsWith('$' + TRIPLE, i)) { state = 'string'; str = { quote: '"', triple: true, template: true }; i += 4; }
            else if (text.startsWith(TRIPLE, i)) { state = 'string'; str = { quote: '"', triple: true, template: false }; i += 3; }
            else if (text.startsWith('$"', i)) { state = 'string'; str = { quote: '"', triple: false, template: true }; i += 2; }
            else if (c === '"' || c === "'") { state = 'string'; str = { quote: c, triple: false, template: false }; i++; }
            else if (c === '{' && interpolations.length) { interpolations[interpolations.length - 1][1]++; i++; }
            else if (c === '}' && interpolations.length) {
                const top = interpolations[interpolations.length - 1];
                if (top[1] === 0) { interpolations.pop(); state = 'string'; str = top[0]; } else top[1]--;
                i++;
            } else i++;
        }
    }
    return state !== 'code';
}

// ============================================================
// Diagnostics
// ============================================================

function wordRange(doc, line, column) {
    const l = Math.max(0, Math.min(line - 1, doc.lineCount - 1));
    const lineText = doc.lineAt(l).text;
    const c = Math.max(0, Math.min(column - 1, lineText.length));
    const word = doc.getWordRangeAtPosition(new vscode.Position(l, c));
    if (word) return word;
    return new vscode.Range(l, c, l, Math.min(c + 1, lineText.length));
}

async function check(doc) {
    if (!isDessa(doc)) return;
    const cwd = path.dirname(doc.fileName);
    const version = doc.version;
    const result = await runDessa(['check', doc.fileName, '--json', '--stdin'], doc.getText(), cwd);
    if (result.missing) return warnMissing();
    if (doc.version !== version) return; // a newer check is on its way

    let items;
    try {
        items = JSON.parse(result.stdout);
    } catch {
        output.appendLine(`dessa check failed:\n${result.stdout}${result.stderr}`);
        return;
    }

    // Unused imports: faded (dim), a warning, or nothing (off)
    const unusedImports = vscode.workspace.getConfiguration('dessa').get('unusedImports') || 'dim';
    const byFile = new Map();
    for (const d of items) {
        if (d.tag === 'unused' && unusedImports === 'off') continue;
        const file = d.file ? path.resolve(cwd, d.file) : doc.fileName;
        const key = vscode.Uri.file(file).toString();
        if (!byFile.has(key)) byFile.set(key, []);
        let range;
        const target = vscode.workspace.textDocuments.find((t) => t.uri.toString() === key);
        const line = Math.max(0, (d.line || 1) - 1), column = Math.max(0, (d.column || 1) - 1);
        if (d.length > 0) range = new vscode.Range(line, column, line, column + d.length);
        else if (target) range = wordRange(target, d.line || 1, d.column || 1);
        else range = new vscode.Range(line, column, line, 1000);
        const severity = d.severity === 'warning' ? vscode.DiagnosticSeverity.Warning
            : d.severity === 'note' ? vscode.DiagnosticSeverity.Information
            : d.severity === 'hint' ? (unusedImports === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Hint)
            : vscode.DiagnosticSeverity.Error;
        const diag = new vscode.Diagnostic(range, d.message, severity);
        diag.source = 'dessa';
        if (d.tag === 'unused') diag.tags = [vscode.DiagnosticTag.Unnecessary];
        byFile.get(key).push(diag);
    }

    const own = doc.uri.toString();
    for (const uri of reportedFiles.get(own) || []) {
        if (!byFile.has(uri)) diagnosticCollection.delete(vscode.Uri.parse(uri));
    }
    if (!byFile.has(own)) byFile.set(own, []);
    for (const [uri, list] of byFile) diagnosticCollection.set(vscode.Uri.parse(uri), list);
    reportedFiles.set(own, [...byFile.keys()]);
}

function scheduleCheck(doc, delay) {
    if (!isDessa(doc)) return;
    const key = doc.uri.toString();
    clearTimeout(timers.get(key));
    timers.set(key, setTimeout(() => {
        timers.delete(key);
        check(doc);
        symbolCache.delete(key);
    }, delay));
}

// ============================================================
// Symbols
// ============================================================

async function symbols(doc) {
    const key = doc.uri.toString();
    const cached = symbolCache.get(key);
    if (cached && cached.version === doc.version) return cached.data;
    const result = await runDessa(['symbols', doc.fileName, '--stdin'], doc.getText(), path.dirname(doc.fileName));
    if (result.missing) {
        warnMissing();
        return cached ? cached.data : null;
    }
    try {
        const data = JSON.parse(result.stdout);
        symbolCache.set(key, { version: doc.version, data });
        return data;
    } catch {
        return cached ? cached.data : null;
    }
}

// Local declarations found by scanning the text before `offset`:
// var[T] x, const[T] x, parameters x[T], for (var[T] x in ...), catch (e[T]).
function localTypes(text) {
    const types = new Map();
    const typePart = '((?:[^\\[\\]]|\\[\\d+\\])*)';
    const declaration = new RegExp(`\\b(?:var|const)\\s*\\[${typePart}\\]\\s*([A-Za-z_]\\w*)`, 'g');
    const parameter = new RegExp(`\\b([A-Za-z_]\\w*)\\s*\\[${typePart}\\]`, 'g');
    let m;
    while ((m = parameter.exec(text))) {
        if (!['var', 'const', 'func', 'class'].includes(m[1])) types.set(m[1], m[2].trim());
    }
    while ((m = declaration.exec(text))) types.set(m[2], m[1].trim());
    return types;
}

function enclosingClass(text) {
    const re = /^\s*class\s*(?:\[[^\]]*\])?\s*([A-Za-z_]\w*)/gm;
    let m;
    let last = null;
    while ((m = re.exec(text))) last = m[1];
    return last;
}

// The declared type of `receiver` in `receiver.member`, as far as we can
// tell from the text before the cursor and the compiler's symbol table.
function receiverType(doc, before, receiver, data) {
    if (receiver === 'this') return { type: enclosingClass(before), isTypeName: false };
    if (receiver === 'super') {
        const cls = enclosingClass(before);
        const m = cls && new RegExp(`class\\s*(?:\\[[^\\]]*\\])?\\s*${cls}\\s*\\(\\s*(\\w+)`).exec(doc.getText());
        return { type: m ? m[1] : undefined, isTypeName: false };
    }
    const local = localTypes(before).get(receiver);
    if (local) return { type: local, isTypeName: false };
    if (data) {
        const g = data.globals.find((s) => s.name === receiver);
        if (g) return { type: g.type, isTypeName: g.kind === 'enum' };
    }
    return { type: undefined, isTypeName: false };
}

// Which `...Methods` class in runtime/decl/methods.ds documents a builtin type.
function methodsClassFor(type, data) {
    const t = stripNil(type || '');
    if (t === 'Str') return 'StrMethods';
    if (t === 'Chr') return 'ChrMethods';
    if (t === 'Bul') return 'BulMethods';
    if (/^(Int|Flt|Dbl)(\[\d+\])?$/.test(t)) return 'NumberMethods';
    if (t.startsWith('Map[')) return 'MapMethods';
    if (t.endsWith('<>')) return 'ListMethods';
    if (t.endsWith('{}')) return 'SetMethods';
    const user = findType(data, t);
    if (user && user.kind === 'enum') return 'EnumMethods';
    return null;
}

// Location of `func[...] name(` / `const[...] name` in a declaration file,
// optionally only inside `class <inClass> { ... }`.
async function findDeclaration(file, name, inClass) {
    let doc;
    try {
        doc = await vscode.workspace.openTextDocument(vscode.Uri.file(file));
    } catch {
        return null;
    }
    const decl = new RegExp(`\\b(?:func\\s*\\[(?:[^\\[\\]]|\\[\\d+\\])*\\]|const\\s*\\[[^\\]]*\\])\\s*(${name})\\b`);
    let inside = !inClass;
    for (let i = 0; i < doc.lineCount; i++) {
        const text = doc.lineAt(i).text;
        if (inClass) {
            const cls = /^class\s+(\w+)/.exec(text);
            if (cls) inside = cls[1] === inClass;
            if (!inside) continue;
        }
        const m = decl.exec(text);
        if (m) return new vscode.Location(doc.uri, new vscode.Position(i, text.indexOf(name, m.index)));
    }
    return null;
}

// `class Name` / `interface Name` / `enum Name` in another file.
async function findClassDeclaration(file, name) {
    let doc;
    try {
        doc = await vscode.workspace.openTextDocument(vscode.Uri.file(file));
    } catch {
        return null;
    }
    const decl = new RegExp(`\\b(?:class|interface|enum)\\s*(?:\\[[^\\]]*\\])?\\s*(${name})\\b`);
    for (let i = 0; i < doc.lineCount; i++) {
        const text = doc.lineAt(i).text;
        const m = decl.exec(text);
        if (m) return new vscode.Location(doc.uri, new vscode.Position(i, text.indexOf(name, m.index)));
    }
    return null;
}

function stripNil(type) {
    return type.split('|').map((t) => t.trim()).filter((t) => t && t !== 'Nil').join(' | ');
}

function findType(data, name) {
    return data && data.types ? data.types.find((t) => t.name === name) : undefined;
}

function paramText(p) {
    return `${p.name}[${p.type}${p.variadic ? '*' : ''}]` + (p.default !== undefined ? `=${p.default}` : '');
}

function signatureOf(s) {
    if (s.params) {
        const params = s.params.map(paramText).join(', ');
        return `(${params}) := ${s.returns}`;
    }
    return s.type || '';
}

// ============================================================
// Compiler-powered IntelliSense
//   dessa complete <file> --stdin --at L:C   what can go at the cursor
//   dessa hover    <file> --stdin --at L:C   the symbol at a position
// Both run the real analyser (types, narrowing, inheritance, imports), so
// `customer.` lists exactly the members of customer's type. When the
// compiler can't make sense of the code the regex fallbacks below are used.
// ============================================================

// Answers are kept per document version: hovering, go to definition and
// parameter hints at the same spot ask the compiler only once.
const queryCache = new Map();

function compilerQuery(doc, command, position, extra = []) {
    const at = `${position.line + 1}:${position.character + 1}`;
    const key = `${doc.uri.toString()}|${doc.version}|${command}|${at}|${extra.join(' ')}`;
    if (queryCache.has(key)) return queryCache.get(key);
    const answer = (async () => {
        const result = await runDessa([command, doc.fileName, '--stdin', '--at', at, ...extra], doc.getText(), path.dirname(doc.fileName));
        if (result.missing) {
            warnMissing();
            return null;
        }
        try {
            const data = JSON.parse(result.stdout);
            return data && data.found ? data : null;
        } catch {
            return null;
        }
    })();
    queryCache.set(key, answer);
    if (queryCache.size > 200) queryCache.delete(queryCache.keys().next().value);
    return answer;
}

// Where a symbol the compiler reported is written: its own file and line,
// or (builtins) the declaration files in runtime/decl and include/.
async function locationOf(doc, symbol, memberOf) {
    if (symbol.line > 0) {
        const name = symbol.kind === 'method' && symbol.name === 'init' && memberOf ? 'init' : symbol.name;
        return locate(symbol.file || doc.fileName, symbol.line, name);
    }
    const data = await symbols(doc);
    if (!data || !data.declarations) return null;
    if (memberOf) {
        const cls = methodsClassFor(memberOf, data);
        return cls ? findDeclaration(path.join(data.declarations, 'methods.ds'), symbol.name, cls) : null;
    }
    if (symbol.kind === 'builtin' && symbol.library) {
        const file = symbol.library === 'builtins'
            ? path.join(data.declarations, 'builtins.ds')
            : path.join(data.includes || '', symbol.library, `${symbol.library}.ds`);
        return findDeclaration(file, symbol.name, null);
    }
    return null;
}

// `name` on (or just after) 1-based `line` of `file`
async function locate(file, line, name) {
    let target;
    try {
        target = await vscode.workspace.openTextDocument(vscode.Uri.file(file));
    } catch {
        return null;
    }
    const word = new RegExp(`\\b${name.replace(/[^\w]/g, '')}\\b`);
    for (let i = line - 1; i < Math.min(target.lineCount, line + 3); i++) {
        if (i < 0) continue;
        const m = word.exec(target.lineAt(i).text);
        if (m) return new vscode.Location(target.uri, new vscode.Position(i, m.index));
    }
    return new vscode.Location(target.uri, new vscode.Position(Math.max(0, line - 1), 0));
}

// "Defined in webutils.dm, line 71" as a link for hovers
function definedInLink(location) {
    if (!location) return '';
    const uri = location.uri.with({ fragment: `L${location.range.start.line + 1},${location.range.start.character + 1}` });
    return `\n\nDefined in [${path.basename(location.uri.fsPath)}, line ${location.range.start.line + 1}](${uri.toString()})`;
}

// The import ($("lib") or $("module")) at a position, from the compiler's symbols
function importAt(data, position) {
    for (const i of (data && data.imports) || []) {
        if (i.line - 1 === position.line && position.character >= i.column - 1 && position.character <= i.column - 1 + i.length) return i;
    }
    return null;
}

// The comment block right above line `line` (1-based) of a file:
// `// ...` lines or a `/** ... */` block.
async function docCommentAt(file, line) {
    if (!file || !line) return '';
    let doc;
    try {
        doc = await vscode.workspace.openTextDocument(vscode.Uri.file(file));
    } catch {
        return '';
    }
    const lines = [];
    let i = line - 2;
    if (i >= 0 && /\*\/\s*$/.test(doc.lineAt(i).text)) {
        for (; i >= 0; i--) {
            const text = doc.lineAt(i).text;
            lines.unshift(text.replace(/^\s*\/?\*+\/?\s?/, '').replace(/\s*\*\/\s*$/, ''));
            if (/^\s*\/\*/.test(text)) break;
        }
    } else {
        for (; i >= 0; i--) {
            const m = /^\s*\/\/\s?(.*)$/.exec(doc.lineAt(i).text);
            if (!m) break;
            lines.unshift(m[1]);
        }
    }
    // `// ---- Arithmetic ----` headers group declarations; they aren't docs
    return lines.filter((l) => !/^\s*-{3,}.*-{3,}\s*$/.test(l)).join('\n').trim();
}

// Documentation for a symbol reported by the compiler.
async function symbolDocs(doc, symbol, memberOf) {
    if (symbol.line > 0) return docCommentAt(symbol.file || doc.fileName, symbol.line);
    const data = await symbols(doc);
    if (!data || !data.declarations) return '';
    let location = null;
    if (memberOf) {
        // Builtin method: documented in runtime/decl/methods.ds
        const cls = methodsClassFor(memberOf, data);
        if (cls) location = await findDeclaration(path.join(data.declarations, 'methods.ds'), symbol.name, cls);
    } else if (symbol.kind === 'builtin' && symbol.library) {
        const file = symbol.library === 'builtins'
            ? path.join(data.declarations, 'builtins.ds')
            : path.join(data.includes || '', symbol.library, `${symbol.library}.ds`);
        location = await findDeclaration(file, symbol.name, null);
    }
    return location ? docCommentAt(location.uri.fsPath, location.range.start.line + 1) : '';
}

// How a symbol is shown in hovers and completion details, Dessa style.
function declarationText(symbol, owner, hereType) {
    const prefix = owner ? `${owner}.` : '';
    switch (symbol.kind) {
        case 'function': case 'method': case 'builtin':
            if (symbol.params) {
                const params = symbol.params.map(paramText).join(', ');
                if (symbol.name === 'init' && symbol.kind === 'method') return `new ${owner}(${params})`;
                return `func[${symbol.returns}] ${prefix}${symbol.name}(${params})`;
            }
            return `const[${symbol.type}] ${symbol.name}`;
        case 'field': return `${prefix}${symbol.name}[${symbol.type}]`;
        case 'constant': return owner ? `${owner}.${symbol.name}` : `const[${symbol.type}] ${symbol.name}`;
        case 'parameter': return `${symbol.name}[${symbol.type}]   (parameter)`;
        case 'class': case 'interface': case 'enum': return `${symbol.kind} ${symbol.name}`;
        default: {
            const text = `var[${symbol.type}] ${symbol.name}`;
            return hereType && hereType !== symbol.type ? `${text}   // here: ${hereType}` : text;
        }
    }
}

async function compilerCompletions(doc, position) {
    const result = await compilerQuery(doc, 'complete', position);
    if (!result) return null;
    if (result.member && result.objectType === 'Unknown') return null; // let the fallback guess
    const items = [];
    const owner = result.member ? stripNil(result.objectType || '') : '';
    result.items.forEach((s, index) => {
        const isMethod = s.kind === 'method' || (s.kind === 'builtin' && s.params) || s.kind === 'function';
        const kind = result.member && s.kind === 'constant' ? vscode.CompletionItemKind.EnumMember : symbolKind(s.kind);
        const item = new vscode.CompletionItem(s.name, kind);
        item.detail = declarationText(s, result.member ? owner : '');
        // Fields before methods; nearest scope first for plain names
        item.sortText = `${isMethod ? 1 : 0}${String(index).padStart(4, '0')}`;
        item.dessa = { symbol: s, memberOf: result.member ? owner : '' };
        items.push(item);
    });
    // Names only: no keywords, and no () added after functions
    if (!result.member) {
        for (const [p, desc] of Object.entries(PRIMITIVES)) {
            const item = new vscode.CompletionItem(p, vscode.CompletionItemKind.TypeParameter);
            item.detail = desc;
            item.sortText = `3${p}`;
            items.push(item);
        }
    }
    return items;
}

async function compilerHover(doc, position) {
    const result = await compilerQuery(doc, 'hover', position);
    if (!result) return null;
    const s = result.symbol;
    if (s.kind === 'class' || s.kind === 'interface' || s.kind === 'enum') return null; // describe() lists members
    let text = '```dessa\n' + declarationText(s, result.memberOf ? stripNil(result.memberOf) : '', result.type) + '\n```';
    const docs = await symbolDocs(doc, s, result.memberOf);
    if (docs) text += '\n\n' + docs;
    return text + definedInLink(await locationOf(doc, s, result.memberOf));
}

// Hovering $("webutils") / $("utils.strings"): what the module is, with a link to it
async function importHover(doc, position) {
    const imp = importAt(await symbols(doc), position);
    if (!imp) return null;
    let summary = '';
    try {
        const target = await vscode.workspace.openTextDocument(vscode.Uri.file(imp.file));
        const m = /\/\*\*\*?([\s\S]*?)\*\//.exec(target.getText().slice(0, 4000));
        if (m) {
            const lines = m[1].split('\n').map((l) => l.replace(/^\s*\*\s?/, '')).filter((l, i, all) => i > 0 || l.trim());
            summary = lines.slice(0, 12).join('\n').trim();
        }
    } catch {
        // no summary
    }
    const what = imp.builtin ? `library "${imp.spec}"` : `module "${imp.spec}"`;
    const link = vscode.Uri.file(imp.file).toString();
    return `**${what}** ([${path.basename(imp.file)}](${link}))` + (summary ? '\n\n```text\n' + summary + '\n```' : '');
}

// ---- Signature help: greet("Ann", |) ----

// The open `(` of the call the cursor is in, and which argument it is on.
function enclosingCall(text) {
    let depth = 0;
    let commas = 0;
    let quote = null;
    for (let i = text.length - 1; i >= 0; i--) {
        const ch = text[i];
        if (quote) {
            if (ch === quote && text[i - 1] !== '\\') quote = null;
            continue;
        }
        if (ch === '"' || ch === "'") quote = ch;
        else if (ch === ')' || ch === ']' || ch === '}') depth++;
        else if (ch === '(' || ch === '[' || ch === '{') {
            if (depth === 0) return ch === '(' ? { open: i, argument: commas } : null;
            depth--;
        } else if (ch === ',' && depth === 0) commas++;
        else if (ch === ';' && depth === 0) return null;
    }
    return null;
}

const signatureHelpProvider = {
    async provideSignatureHelp(doc, position) {
        if (!inDessaScript(doc, position)) return null;
        const start = doc.offsetAt(position) > 4000 ? doc.positionAt(doc.offsetAt(position) - 4000) : new vscode.Position(0, 0);
        const before = doc.getText(new vscode.Range(start, position));
        const call = enclosingCall(before);
        if (!call) return null;
        const name = /([A-Za-z_]\w*)\s*$/.exec(before.slice(0, call.open));
        if (!name) return null;
        // On the last letter of the name (the compiler matches the whole word)
        const namePos = doc.positionAt(doc.offsetAt(start) + name.index + name[1].length - 1);
        const cursor = `${position.line + 1}:${position.character + 1}`;
        const result = await compilerQuery(doc, 'hover', namePos, ['--cursor', cursor]);
        if (!result || !result.symbol.params) return null;
        const s = result.symbol;
        const label = declarationText(s, result.memberOf ? stripNil(result.memberOf) : '');
        const info = new vscode.SignatureInformation(label);
        const docs = await symbolDocs(doc, s, result.memberOf);
        if (docs) info.documentation = new vscode.MarkdownString(docs);
        // Parameter label ranges inside the signature label
        let from = label.indexOf('(') + 1;
        info.parameters = s.params.map((p) => {
            const text = paramText(p);
            const at = label.indexOf(text, from);
            from = at + text.length;
            return new vscode.ParameterInformation([at, at + text.length]);
        });
        const help = new vscode.SignatureHelp();
        help.signatures = [info];
        help.activeSignature = 0;
        const variadic = s.params.length && s.params[s.params.length - 1].variadic;
        help.activeParameter = variadic ? Math.min(call.argument, s.params.length - 1) : call.argument;
        return help;
    },
};

function memberItems(data, typeName, receiverIsTypeName) {
    const items = [];
    const add = (name, detail, kind) => {
        const item = new vscode.CompletionItem(name, kind);
        item.detail = detail;
        items.push(item);
    };
    const t = stripNil(typeName || '');
    const userType = findType(data, t);

    if (userType && userType.kind === 'enum') {
        if (receiverIsTypeName) {
            for (const m of userType.members) add(m.name, `${t}.${m.name}`, vscode.CompletionItemKind.EnumMember);
        } else {
            for (const [n, s] of METHODS.Enum) add(n, s, vscode.CompletionItemKind.Method);
        }
        return items;
    }
    if (userType) {
        for (const m of userType.members) {
            const isMethod = m.kind === 'method';
            add(m.name, isMethod ? signatureOf(m) : m.type, isMethod ? vscode.CompletionItemKind.Method : vscode.CompletionItemKind.Field);
        }
        return items;
    }
    const methodSets = [];
    if (t === 'Str') methodSets.push(METHODS.Str);
    else if (t === 'Chr') methodSets.push(METHODS.Chr);
    else if (/^(Int|Flt|Dbl)(\[\d+\])?$/.test(t)) methodSets.push(METHODS.Num);
    else if (t === 'Bul') methodSets.push(METHODS.Bul);
    else if (t.startsWith('Map[')) methodSets.push(METHODS.Map);
    else if (t.endsWith('<>')) methodSets.push(METHODS.Collection, METHODS.List);
    else if (t.endsWith('{}')) methodSets.push(METHODS.Collection, METHODS.Set);
    for (const set of methodSets) for (const [n, s] of set) add(n, s, vscode.CompletionItemKind.Method);
    return items;
}

// The project's Dessa files as import names (utils/strings.ds -> utils.strings),
// next to the edited file
function projectModules(doc) {
    const root = path.dirname(doc.fileName);
    const own = path.resolve(doc.fileName);
    const found = [];
    const walk = (folder, prefix, depth) => {
        let entries = [];
        try { entries = fs.readdirSync(folder, { withFileTypes: true }); } catch { return; }
        for (const e of entries) {
            if (e.name.startsWith('.') || e.name === 'riv_modules' || e.name === 'node_modules') continue;
            const full = path.join(folder, e.name);
            if (e.isDirectory()) {
                if (depth < 3) walk(full, prefix + e.name + '.', depth + 1);
            } else if (/\.(ds|dm|dx|dz|dsx|dxx|dzx|dds|dss)$/.test(e.name) && path.resolve(full) !== own) {
                found.push(prefix + e.name.replace(/\.[^.]+$/, ''));
            }
        }
    };
    walk(root, '', 0);
    return [...new Set(found)].sort();
}

// The names `$("spec") { ... }` can import: a library's, or a file's
// (without the _private ones and the builtins everyone has)
async function importableNames(doc, spec, data) {
    const names = new Map();
    const add = (s) => {
        if (s.name.startsWith('_') || names.has(s.name)) return;
        names.set(s.name, s);
    };
    const lib = data && data.libraries ? data.libraries.find((l) => l.name === spec) : null;
    if (lib) lib.members.forEach(add);
    let file = null;
    const imported = data && data.imports ? data.imports.find((i) => i.spec === spec && i.file) : null;
    if (imported) file = imported.file;
    if (!file && !lib) {
        const base = path.join(path.dirname(doc.fileName), ...spec.split('.'));
        for (const ext of ['.ds', '.dm', '.dx', '.dz', '.dsx', '.dxx', '.dzx', '.dds', '.dss']) {
            if (fs.existsSync(base + ext)) {
                file = base + ext;
                break;
            }
        }
    }
    if (file) {
        const result = await runDessa(['symbols', file], undefined, path.dirname(file));
        try {
            for (const g of JSON.parse(result.stdout).globals) if (g.kind !== 'builtin') add(g);
        } catch {
            // not readable right now: what the library lists is enough
        }
    }
    return [...names.values()];
}

// $ at the start of a line becomes $(|); a space right after var, const or
// func becomes [|] (a type goes there). Then suggestions open.
function autoBrackets(e) {
    if (!vscode.workspace.getConfiguration('dessa').get('autoBrackets')) return;
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document !== e.document || e.contentChanges.length !== 1) return;
    const change = e.contentChanges[0];
    if (change.rangeLength !== 0 || (change.text !== '$' && change.text !== ' ')) return;
    const doc = e.document;
    const at = change.range.start;
    if (!isDessa(doc) || !inDessaScript(doc, at) || inStringOrComment(doc, at)) return;
    const text = doc.lineAt(at.line).text;
    const before = text.slice(0, at.character);
    const after = text.slice(at.character + 1);
    let insert = null;
    if (change.text === '$' && /^\s*$/.test(before) && after.trim() === '') insert = '()';
    if (change.text === ' ' && /(^|[\s;{(])(var|const|func)$/.test(before) && after.trim() === '') insert = '[]';
    if (!insert) return;
    const end = at.translate(0, 1);
    editor.edit((b) => {
        if (insert === '[]') b.replace(new vscode.Range(at, end), '[]');
        else b.insert(end, '()');
    }, { undoStopBefore: false, undoStopAfter: false }).then((ok) => {
        if (!ok) return;
        const inside = insert === '[]' ? at.translate(0, 1) : end.translate(0, 1);
        editor.selection = new vscode.Selection(inside, inside);
        vscode.commands.executeCommand('editor.action.triggerSuggest');
    });
}

function symbolKind(kind) {
    switch (kind) {
        case 'function': case 'builtin': return vscode.CompletionItemKind.Function;
        case 'class': return vscode.CompletionItemKind.Class;
        case 'interface': return vscode.CompletionItemKind.Interface;
        case 'enum': return vscode.CompletionItemKind.Enum;
        case 'constant': return vscode.CompletionItemKind.Constant;
        case 'method': return vscode.CompletionItemKind.Method;
        case 'field': return vscode.CompletionItemKind.Field;
        case 'module': return vscode.CompletionItemKind.Module;
        default: return vscode.CompletionItemKind.Variable;
    }
}

// ============================================================
// Providers
// ============================================================

const completionProvider = {
    async provideCompletionItems(doc, position, token, context) {
        if (!inDessaScript(doc, position)) return undefined;   // HTML markup: leave it to the HTML extension
        const linePrefix = doc.lineAt(position).text.slice(0, position.character);
        const before = doc.getText(new vscode.Range(new vscode.Position(0, 0), position));
        const data = await symbols(doc);

        // $("dcrypto") { sha| : what the module offers
        const braces = /\$\(\s*"([\w.]+)"(?:\s+as\s+\w+)?\s*\)\s*\{([^}]*)$/.exec(linePrefix);
        if (braces) {
            const inside = braces[2];
            if (/\bas\s+\w*$/.test(inside)) return [];   // naming an alias
            const listed = new Set(inside.split(',').map((s) => s.trim().split(/\s+/)[0]).filter(Boolean));
            const names = await importableNames(doc, braces[1], data);
            return names.filter((s) => !listed.has(s.name)).map((s) => {
                const item = new vscode.CompletionItem(s.name, symbolKind(s.kind));
                item.detail = s.params ? signatureOf(s) : s.type || s.kind;
                return item;
            });
        }
        // { and , only open suggestions inside an import's braces
        if (context && (context.triggerCharacter === '{' || context.triggerCharacter === ',')) return undefined;
        // $ at the start of a line: an import (with auto brackets, () is typed for you)
        if (/^\s*\$$/.test(linePrefix) && !vscode.workspace.getConfiguration('dessa').get('autoBrackets')) {
            const item = new vscode.CompletionItem('$("...")', vscode.CompletionItemKind.Module);
            item.insertText = new vscode.SnippetString('("$1")');
            item.detail = 'import a library or a file';
            item.command = { command: 'editor.action.triggerSuggest', title: 'Suggest' };
            return [item];
        }
        // $("ma  or  $(ma : libraries and the project's files, with the quotes
        const inImport = /\$\(\s*(?:"[\w.]*"(?:\s+as\s+\w+)?\s*,\s*)*("?)([\w.]*)$/.exec(linePrefix);
        if (inImport) {
            const quoted = inImport[1] === '"';
            const typed = inImport[2];
            const replace = new vscode.Range(position.translate(0, -typed.length - (quoted ? 1 : 0)), position);
            const close = doc.lineAt(position).text.slice(position.character).startsWith('"') ? '' : '"';
            const make = (name, kind, detail) => {
                const item = new vscode.CompletionItem(name, kind);
                item.detail = detail;
                item.insertText = '"' + name + (quoted ? close : '"');
                item.range = replace;
                item.filterText = (quoted ? '"' : '') + name;
                return item;
            };
            const items = (data ? data.libraries : []).map((lib) =>
                make(lib.name, vscode.CompletionItemKind.Module, `library: ${lib.members.map((m) => m.name).join(', ')}`));
            for (const module of projectModules(doc)) items.push(make(module, vscode.CompletionItemKind.File, 'a file of this project'));
            return items;
        }
        // A quote typed anywhere else starts an ordinary string
        if (context && context.triggerCharacter === '"') return undefined;
        if (inStringOrComment(doc, position)) return undefined;

        // Ask the compiler first: exact types, locals, narrowing, imports
        const fromCompiler = await compilerCompletions(doc, position);
        if (fromCompiler) return fromCompiler;

        // Fallback (the compiler couldn't analyse this spot): receiver.member
        const member = /([A-Za-z_]\w*)\s*\??\.\s*\w*$/.exec(linePrefix);
        if (member) {
            const { type, isTypeName } = receiverType(doc, before, member[1], data);
            if (type) return memberItems(data, type, isTypeName);
            return [];
        }

        const items = [];
        const seen = new Set();
        const push = (item) => {
            if (seen.has(item.label)) return;
            seen.add(item.label);
            items.push(item);
        };
        for (const [p, desc] of Object.entries(PRIMITIVES)) {
            const item = new vscode.CompletionItem(p, vscode.CompletionItemKind.TypeParameter);
            item.detail = desc;
            push(item);
        }
        for (const [name, type] of localTypes(before)) {
            const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Variable);
            item.detail = type;
            push(item);
        }
        if (data) {
            for (const s of data.globals) {
                const item = new vscode.CompletionItem(s.name, symbolKind(s.kind));
                item.detail = s.params ? signatureOf(s) : s.type;
                push(item);
            }
        }
        return items;
    },

    // Doc comments are looked up only for the item being shown
    async resolveCompletionItem(item) {
        if (!item.dessa) return item;
        const doc = vscode.window.activeTextEditor && vscode.window.activeTextEditor.document;
        if (!doc || !isDessa(doc)) return item;
        const docs = await symbolDocs(doc, item.dessa.symbol, item.dessa.memberOf);
        if (docs) item.documentation = new vscode.MarkdownString(docs);
        return item;
    },
};

async function describe(doc, word) {
    if (PRIMITIVES[word]) return `**${word}**: ${PRIMITIVES[word]}`;
    const data = await symbols(doc);
    if (!data) return null;
    const g = data.globals.find((s) => s.name === word);
    if (g) {
        if (g.kind === 'function' || g.kind === 'builtin') return '```dessa\nfunc ' + word + signatureOf(g) + '\n```';
        if (g.kind === 'class' || g.kind === 'interface' || g.kind === 'enum') {
            const t = findType(data, word);
            const members = t ? t.members.map((m) => (m.kind === 'method' ? `  func ${m.name}${signatureOf(m)}` : `  ${m.name}[${m.type}]`)).join('\n') : '';
            const link = t && t.line > 0 ? definedInLink(await locate(t.file || doc.fileName, t.line, word)) : '';
            return '```dessa\n' + `${g.kind} ${word}` + (members ? ' {\n' + members + '\n}' : '') + '\n```' + link;
        }
        return '```dessa\n' + `${g.kind === 'constant' ? 'const' : 'var'}[${g.type}] ${word}` + '\n```';
    }
    const local = localTypes(doc.getText()).get(word);
    if (local) return '```dessa\n' + `${word}[${local}]` + '\n```';
    for (const t of data.types || []) {
        const m = t.members.find((x) => x.name === word);
        if (m) return '```dessa\n' + `${t.name}.${word}` + (m.kind === 'method' ? signatureOf(m) : `[${m.type}]`) + '\n```';
    }
    return null;
}

// ============================================================
// HTML in .dhtml pages: tag / attribute hover and completion, from the same
// HTML language service VS Code uses for .html (bundled in lib/htmlservice.js)
// ============================================================

let htmlService;
function htmlFor(doc) {
    if (htmlService === undefined) {
        try {
            const h = require('./lib/htmlservice.js');
            htmlService = { h, ls: h.getLanguageService() };
        } catch {
            htmlService = null;
        }
    }
    if (!htmlService) return null;
    const td = htmlService.h.TextDocument.create(doc.uri.toString(), 'html', doc.version, doc.getText());
    return { ls: htmlService.ls, td, parsed: htmlService.ls.parseHTMLDocument(td) };
}

const lspRange = (r) => new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character);
const lspMarkdown = (c) => {
    if (!c) return undefined;
    if (typeof c === 'string') return new vscode.MarkdownString(c);
    if (Array.isArray(c)) return new vscode.MarkdownString(c.map((x) => (typeof x === 'string' ? x : x.value)).join('\n\n'));
    return new vscode.MarkdownString(c.value);
};

// The page's HTML (not inside a Dessa script) of a .dhtml file
function inPageHtml(doc, position) {
    return doc.languageId === 'dessa-html' && !inDessaScript(doc, position);
}

function htmlHover(doc, position) {
    const html = htmlFor(doc);
    if (!html) return null;
    const hover = html.ls.doHover(html.td, position, html.parsed);
    if (!hover) return null;
    return new vscode.Hover(lspMarkdown(hover.contents), hover.range ? lspRange(hover.range) : undefined);
}

const htmlCompletionProvider = {
    provideCompletionItems(doc, position) {
        if (!inPageHtml(doc, position)) return undefined;
        const html = htmlFor(doc);
        if (!html) return undefined;
        const list = html.ls.doComplete(html.td, position, html.parsed);
        const items = list.items.map((i) => {
            const item = new vscode.CompletionItem(i.label, i.kind ? i.kind - 1 : undefined);
            item.documentation = lspMarkdown(i.documentation);
            item.detail = i.detail;
            item.filterText = i.filterText;
            item.sortText = i.sortText;
            const edit = i.textEdit;
            if (edit) {
                item.range = lspRange(edit.range || edit.replace);
                item.insertText = i.insertTextFormat === 2 ? new vscode.SnippetString(edit.newText) : edit.newText;
            }
            if (i.command) item.command = i.command;
            return item;
        });
        return new vscode.CompletionList(items, list.isIncomplete);
    },
};

const htmlHighlightProvider = {
    provideDocumentHighlights(doc, position) {
        if (!inPageHtml(doc, position)) return null;
        const html = htmlFor(doc);
        if (!html) return null;
        return html.ls.findDocumentHighlights(html.td, position, html.parsed)
            .map((h) => new vscode.DocumentHighlight(lspRange(h.range), h.kind ? h.kind - 1 : undefined));
    },
};

const htmlFoldingProvider = {
    provideFoldingRanges(doc) {
        const html = htmlFor(doc);
        if (!html) return [];
        return html.ls.getFoldingRanges(html.td).map((r) => new vscode.FoldingRange(r.startLine, r.endLine));
    },
};

const hoverProvider = {
    async provideHover(doc, position) {
        if (inPageHtml(doc, position)) return htmlHover(doc, position);
        if (!inDessaScript(doc, position)) return null;
        const range = doc.getWordRangeAtPosition(position);
        if (!range) return null;
        const quoted = inStringOrComment(doc, position);   // only an import's name has a hover there
        const text = (await importHover(doc, position)) || (quoted ? null : (await compilerHover(doc, position)) ||
                     (await describe(doc, doc.getText(range))));
        if (!text) return null;
        const markdown = new vscode.MarkdownString(text);
        markdown.isTrusted = true;
        return new vscode.Hover(markdown, range);
    },
};

// Ctrl+click on $("webutils") / $("utils.strings") opens the module
const documentLinkProvider = {
    async provideDocumentLinks(doc) {
        const data = await symbols(doc);
        return ((data && data.imports) || []).map((i) => {
            const range = new vscode.Range(i.line - 1, i.column - 1, i.line - 1, i.column - 1 + i.length);
            const link = new vscode.DocumentLink(range, vscode.Uri.file(i.file));
            link.tooltip = `Open ${path.basename(i.file)}`;
            return link;
        });
    },
};

const definitionProvider = {
    async provideDefinition(doc, position) {
        if (!inDessaScript(doc, position)) return null;
        // An import: the module file
        const imp = importAt(await symbols(doc), position);
        if (imp) return new vscode.Location(vscode.Uri.file(imp.file), new vscode.Position(0, 0));
        if (inStringOrComment(doc, position)) return null;
        // Anything the compiler can resolve: variables, functions, fields,
        // methods (win.label), constructors (new Window), builtins
        const hover = await compilerQuery(doc, 'hover', position);
        if (hover) {
            const location = await locationOf(doc, hover.symbol, hover.memberOf);
            if (location) return location;
        }
        const range = doc.getWordRangeAtPosition(position);
        if (!range) return null;
        const word = doc.getText(range);
        // A type name (var[Window] w): the class / enum / interface
        const typeInfo = findType(await symbols(doc), word);
        if (typeInfo && typeInfo.line > 0) return locate(typeInfo.file || doc.fileName, typeInfo.line, word);
        return fallbackDefinition(doc, position, range, word);
    },
};

// When the compiler can't resolve it (the code doesn't parse yet): search the text
async function fallbackDefinition(doc, position, range, word) {
    {
        // Declarations in this file
        const patterns = [
            new RegExp(`\\b(?:class|interface|enum)\\s*(?:\\[[^\\]]*\\])?\\s*${word}\\b`),
            new RegExp(`\\bfunc\\s*\\[[^\\]]*(?:\\[\\d+\\])?[^\\]]*\\]\\s*${word}\\s*\\(`),
            new RegExp(`\\b(?:var|const)\\s*\\[(?:[^\\[\\]]|\\[\\d+\\])*\\]\\s*${word}\\b`),
        ];
        const linePrefix = doc.lineAt(position).text.slice(0, range.start.character);
        const isMember = /\??\.\s*$/.test(linePrefix);

        if (!isMember) {
            for (let i = 0; i < doc.lineCount; i++) {
                const text = doc.lineAt(i).text;
                for (const p of patterns) {
                    const m = p.exec(text);
                    if (m) {
                        const col = text.indexOf(word, m.index + m[0].length - word.length - 1);
                        return new vscode.Location(doc.uri, new vscode.Position(i, Math.max(0, col)));
                    }
                }
            }
        }

        // Builtins: open the declaration file in runtime/decl
        const data = await symbols(doc);
        if (!data || !data.declarations) return null;
        if (isMember) {
            const receiver = /([A-Za-z_]\w*)\s*\??\.\s*$/.exec(linePrefix);
            if (!receiver) return null;
            const before = doc.getText(new vscode.Range(new vscode.Position(0, 0), range.start));
            const { type } = receiverType(doc, before, receiver[1], data);
            const cls = methodsClassFor(type, data);
            return cls ? findDeclaration(path.join(data.declarations, 'methods.ds'), word, cls) : null;
        }
        const symbol = data.globals.find((s) => s.name === word);
        if (symbol && symbol.kind === 'builtin' && symbol.library) {
            // print/input: runtime/decl/builtins.ds; libraries: include/<lib>/<lib>.ds
            const file = symbol.library === 'builtins'
                ? path.join(data.declarations, 'builtins.ds')
                : path.join(data.includes || '', symbol.library, `${symbol.library}.ds`);
            return findDeclaration(file, word, null);
        }
        if (symbol && symbol.file) {
            // Imported from another .ds / .dm file (including Dessa-written libraries)
            const location = await findDeclaration(symbol.file, word, null);
            if (location) return location;
            const classDecl = await findClassDeclaration(symbol.file, word);
            if (classDecl) return classDecl;
        }
        return null;
    }
}

const documentSymbolProvider = {
    provideDocumentSymbols(doc) {
        const result = [];
        const re = /^\s*(?:(class|interface|enum)\s*(?:\[[^\]]*\])?\s*([A-Za-z_]\w*)|func\s*\[(?:[^\[\]]|\[\d+\])*\]\s*([A-Za-z_]\w*)|(init)\s*\()/;
        let container = null;
        for (let i = 0; i < doc.lineCount; i++) {
            const line = doc.lineAt(i);
            const m = re.exec(line.text);
            if (!m) continue;
            const indented = /^\s+/.test(line.text);
            if (m[1]) {
                const kind = m[1] === 'class' ? vscode.SymbolKind.Class : m[1] === 'interface' ? vscode.SymbolKind.Interface : vscode.SymbolKind.Enum;
                container = new vscode.DocumentSymbol(m[2], m[1], kind, line.range, line.range);
                result.push(container);
            } else {
                const name = m[3] || m[4];
                const symbol = new vscode.DocumentSymbol(name, '', m[4] ? vscode.SymbolKind.Constructor : indented ? vscode.SymbolKind.Method : vscode.SymbolKind.Function, line.range, line.range);
                if (indented && container) container.children.push(symbol);
                else {
                    container = null;
                    result.push(symbol);
                }
            }
        }
        return result;
    },
};

// ============================================================
// Commands
// ============================================================

async function activeDessaDocument() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || !isDessa(editor.document)) {
        vscode.window.showErrorMessage('Open a saved .ds or .dhtml file first.');
        return null;
    }
    if (editor.document.isDirty) await editor.document.save();
    return editor.document;
}

async function runFile() {
    const doc = await activeDessaDocument();
    if (!doc) return;
    const terminal = vscode.window.createTerminal({
        name: `Dessa: ${path.basename(doc.fileName)}`,
        shellPath: compilerPath(),
        shellArgs: ['run', doc.fileName],
        cwd: path.dirname(doc.fileName),
        env: { DESSA_PACKAGES: compilerEnv().DESSA_PACKAGES },
    });
    terminal.show();
}

async function showCpp() {
    const doc = await activeDessaDocument();
    if (!doc) return;
    const result = await runDessa(['emit', doc.fileName], undefined, path.dirname(doc.fileName));
    if (result.missing) return warnMissing();
    if (result.code !== 0) {
        vscode.window.showErrorMessage('The file has errors; fix them first (see the Problems panel).');
        return;
    }
    const cpp = await vscode.workspace.openTextDocument({ language: 'cpp', content: result.stdout });
    vscode.window.showTextDocument(cpp, vscode.ViewColumn.Beside);
}

async function checkCommand() {
    const doc = await activeDessaDocument();
    if (!doc) return;
    await check(doc);
    const problems = diagnosticCollection.get(doc.uri) || [];
    const errors = problems.filter((d) => d.severity === vscode.DiagnosticSeverity.Error).length;
    vscode.window.showInformationMessage(errors ? `Dessa: ${errors} error(s) found.` : 'Dessa: no errors.');
}

// ============================================================
// Activation
// ============================================================

function activate(context) {
    diagnosticCollection = vscode.languages.createDiagnosticCollection('dessa');
    output = vscode.window.createOutputChannel('Dessa');
    // Dessa files, .dhtml pages, and Dessa scripts inside HTML pages
    const selector = [
        { language: 'dessa', scheme: 'file' },
        { language: 'dessa-html', scheme: 'file' },
        { language: 'html', scheme: 'file' },
    ];
    const dessaOnly = { language: 'dessa', scheme: 'file' };

    context.subscriptions.push(
        diagnosticCollection,
        output,
        vscode.languages.registerCompletionItemProvider(selector, completionProvider, '.', '"', '$', '{', ','),
        vscode.languages.registerHoverProvider(selector, hoverProvider),
        vscode.languages.registerSignatureHelpProvider(selector, signatureHelpProvider, '(', ','),
        vscode.languages.registerDefinitionProvider(selector, definitionProvider),
        vscode.languages.registerDocumentLinkProvider(selector, documentLinkProvider),
        // The HTML of .dhtml pages
        vscode.languages.registerCompletionItemProvider({ language: 'dessa-html' }, htmlCompletionProvider, '<', ' ', '"', "'", '=', '/', ':'),
        vscode.languages.registerDocumentHighlightProvider({ language: 'dessa-html' }, htmlHighlightProvider),
        vscode.languages.registerFoldingRangeProvider({ language: 'dessa-html' }, htmlFoldingProvider),
        vscode.languages.registerDocumentSymbolProvider(dessaOnly, documentSymbolProvider),
        vscode.commands.registerCommand('dessa.run', runFile),
        vscode.commands.registerCommand('dessa.showCpp', showCpp),
        vscode.commands.registerCommand('dessa.check', checkCommand),
        vscode.workspace.onDidOpenTextDocument((doc) => scheduleCheck(doc, 0)),
        vscode.workspace.onDidSaveTextDocument((doc) => scheduleCheck(doc, 0)),
        vscode.workspace.onDidChangeTextDocument((e) => {
            autoBrackets(e);
            if (vscode.workspace.getConfiguration('dessa').get('checkOnType')) scheduleCheck(e.document, 400);
        }),
        vscode.workspace.onDidCloseTextDocument((doc) => {
            const key = doc.uri.toString();
            for (const uri of reportedFiles.get(key) || []) diagnosticCollection.delete(vscode.Uri.parse(uri));
            reportedFiles.delete(key);
            symbolCache.delete(key);
        }),
        vscode.workspace.onDidChangeConfiguration((e) => {
            if (e.affectsConfiguration('dessa.compilerPath')) {
                warnedMissingCompiler = false;
                vscode.workspace.textDocuments.forEach((d) => scheduleCheck(d, 0));
            }
        })
    );

    vscode.workspace.textDocuments.forEach((doc) => scheduleCheck(doc, 0));
}

function deactivate() {
    for (const t of timers.values()) clearTimeout(t);
}

module.exports = { activate, deactivate };
