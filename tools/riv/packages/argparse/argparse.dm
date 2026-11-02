/***
 * $("argparse") -- command-line arguments: flags, options and positionals,
 * with --help made for you.
 *
 *     $("argparse") { ArgParser, Args }
 *     $("sys") { args }
 *
 *     var[ArgParser] parser = new ArgParser("greet", "Says hello to someone");
 *     parser.flag("loud", "l", "shout the greeting");
 *     parser.option("name", "n", "who to greet", "World");
 *     parser.option("times", "t", "how many times", "1");
 *     parser.positional("file", "where to write it (optional)", false);
 *
 *     var[Args] a = parser.parse(args());
 *     for (var[Int] i = 0; i < a.getInt("times"); i++) {
 *         var[Str] text = $"Hello, {a.get("name")}!";
 *         print(a.has("loud") ? text.upper() : text);
 *     }
 *
 *     dessa run greet.ds -- --name Ann -l          HELLO, ANN!
 *     dessa run greet.ds -- -t 2 --name=Bo         Hello, Bo! (twice)
 *     dessa run greet.ds -- --help                 the usage, then exits
 *
 * Options are written --name value, --name=value or -n value. Short flags
 * can be joined: -lv is -l -v. Everything after -- is a positional.
 * A mistake (unknown option, missing value, missing positional) prints what
 * is wrong and the usage line, and ends the program with exit code 2.
 */

$("sys") { exit }

class _Option {
    var[Str] name = "";
    var[Str] short = "";
    var[Str] help = "";
    var[Bul] takesValue = false;
    var[Str | Nil] fallback = nil;
}

class _Positional {
    var[Str] name = "";
    var[Str] help = "";
    var[Bul] required = true;
    var[Bul] many = false;
}

// What parse() found
class Args {
    var[Map[Str, Str]] _values = {:};
    var[Str{}] _flags = {};
    var[Map[Str, Str<>]] _lists = {:};
    var[Str] _program = "";

    // Was the flag given? (-l / --loud)
    func[Bul] has(name[Str]) {
        return this._flags.includes(name) || this._values.has(name);
    }

    // An option's or positional's value (its default when not given). Error
    // if it has neither.
    func[Str] get(name[Str]) {
        if (!this._values.has(name)) throw new ArgError($"{this._program}: '{name}' was not given and has no default");
        return this._values<name>;
    }

    // The value, or nil when it wasn't given and has no default
    func[Str | Nil] find(name[Str]) {
        return this._values.has(name) ? this._values<name> : nil;
    }

    // The value as a whole number; a value that isn't one ends the program
    // with a message, like any other mistake on the command line
    func[Int] getInt(name[Str]) {
        var[Str] text = this.get(name);
        try {
            return Int(text);
        } catch (e[Str]) {
            _fail(this._program, $"'{name}' must be a whole number, got \"{text}\"", "");
        }
        return 0;
    }

    func[Dbl] getDbl(name[Str]) {
        var[Str] text = this.get(name);
        try {
            return Dbl(text);
        } catch (e[Str]) {
            _fail(this._program, $"'{name}' must be a number, got \"{text}\"", "");
        }
        return 0;
    }

    // Every value of a positional made with many = true (empty if none)
    func[Str<>] list(name[Str]) {
        return this._lists.has(name) ? this._lists<name> : <>;
    }
}

class ArgError(Error) {}

func[Emp] _fail(program[Str], message[Str], usage[Str]) {
    print($"{program}: {message}");
    if (usage != "") print(usage);
    exit(2);
}

class ArgParser {
    var[Str] program = "";
    var[Str] description = "";
    var[_Option<>] _options = <>;
    var[_Positional<>] _positionals = <>;

    init(program[Str], description[Str] = "") {
        this.program = program;
        this.description = description;
    }

    // An on/off switch: --name (and -short, when short isn't "")
    func[Emp] flag(name[Str], short[Str], help[Str]) {
        this._add(name, short, help, false, nil);
    }

    // A named value: --name value. `fallback` is used when it isn't given;
    // without one (nil) the option is optional and a.find(name) is nil.
    func[Emp] option(name[Str], short[Str], help[Str], fallback[Str | Nil] = nil) {
        this._add(name, short, help, true, fallback);
    }

    // A value given by its place: the first positional takes the first
    // word that isn't an option, and so on
    func[Emp] positional(name[Str], help[Str], required[Bul] = true) {
        var[_Positional] p = new _Positional();
        p.name = name;
        p.help = help;
        p.required = required;
        this._positionals.push(p);
    }

    // The words left over after the other positionals (a.list(name))
    func[Emp] rest(name[Str], help[Str]) {
        var[_Positional] p = new _Positional();
        p.name = name;
        p.help = help;
        p.required = false;
        p.many = true;
        this._positionals.push(p);
    }

    func[Emp] _add(name[Str], short[Str], help[Str], takesValue[Bul], fallback[Str | Nil]) {
        var[_Option] o = new _Option();
        o.name = name;
        o.short = short;
        o.help = help;
        o.takesValue = takesValue;
        o.fallback = fallback;
        this._options.push(o);
    }

    // One line: usage: greet [-l] [--name NAME] file
    func[Str] usage() {
        var[Str] out = $"usage: {this.program}";
        for (var[_Option] o in this._options) {
            var[Str] spelled = o.short != "" ? $"-{o.short}" : $"--{o.name}";
            out += o.takesValue ? $" [{spelled} {o.name.upper()}]" : $" [{spelled}]";
        }
        for (var[_Positional] p in this._positionals) {
            if (p.many) out += $" [{p.name}...]";
            else if (p.required) out += $" {p.name}";
            else out += $" [{p.name}]";
        }
        return out;
    }

    // The whole --help text
    func[Str] help() {
        var[Str] out = this.usage();
        if (this.description != "") out += $"\n\n{this.description}";
        if (!this._positionals.isEmpty()) {
            out += "\n\npositionals:";
            for (var[_Positional] p in this._positionals) out += "\n  " + _pad(p.name, 22) + p.help;
        }
        out += "\n\noptions:\n  " + _pad("-h, --help", 22) + "show this help and exit";
        for (var[_Option] o in this._options) {
            var[Str] spelled = (o.short != "" ? $"-{o.short}, " : "    ") + $"--{o.name}";
            if (o.takesValue) spelled += " " + o.name.upper();
            var[Str] text = o.help;
            if (o.fallback != nil) text += $" (default: {o.fallback})";
            out += "\n  " + _pad(spelled, 22) + text;
        }
        return out;
    }

    func[_Option | Nil] _long(name[Str]) {
        for (var[_Option] o in this._options) if (o.name == name) return o;
        return nil;
    }

    func[_Option | Nil] _short(name[Str]) {
        for (var[_Option] o in this._options) if (o.short == name) return o;
        return nil;
    }

    // Reads the words (usually args()). --help prints the help and exits;
    // a mistake prints it and exits with code 2.
    func[Args] parse(words[Str<>]) {
        var[Args] found = new Args();
        found._program = this.program;
        var[Str<>] loose = <>;
        var[Bul] onlyPositionals = false;
        var[Int] i = 0;
        while (i < words.length()) {
            var[Str] word = words<i>;
            i++;
            if (onlyPositionals || word == "-" || !word.startsWith("-")) {
                loose.push(word);
                continue;
            }
            if (word == "--") {
                onlyPositionals = true;
                continue;
            }
            if (word == "-h" || word == "--help") {
                print(this.help());
                exit(0);
            }
            if (word.startsWith("--")) {
                var[Str] name = word.substr(2, word.length() - 2);
                var[Str | Nil] inline = nil;
                var[Int] eq = name.indexOf("=");
                if (eq >= 0) {
                    inline = name.substr(eq + 1, name.length() - eq - 1);
                    name = name.substr(0, eq);
                }
                var[_Option | Nil] o = this._long(name);
                if (o == nil) _fail(this.program, $"unknown option --{name}", this.usage());
                else i = this._take(o, inline, words, i, found, $"--{name}");
                continue;
            }
            // -l, -n value, -nvalue, -lv
            var[Int] k = 1;
            while (k < word.length()) {
                var[Str] letter = word.substr(k, 1);
                var[_Option | Nil] o = this._short(letter);
                if (o == nil) {
                    _fail(this.program, $"unknown option -{letter}", this.usage());
                } else if (o.takesValue) {
                    var[Str | Nil] inline = k + 1 < word.length() ? word.substr(k + 1, word.length() - k - 1) : nil;
                    i = this._take(o, inline, words, i, found, $"-{letter}");
                    k = word.length();
                } else {
                    found._flags.add(o.name);
                    k++;
                }
            }
        }

        // defaults
        for (var[_Option] o in this._options) {
            var[Str | Nil] fallback = o.fallback;
            if (o.takesValue && !found._values.has(o.name) && fallback != nil) found._values<o.name> = fallback;
        }
        // positionals, in order
        var[Int] next = 0;
        for (var[_Positional] p in this._positionals) {
            if (p.many) {
                var[Str<>] all = <>;
                while (next < loose.length()) {
                    all.push(loose<next>);
                    next++;
                }
                found._lists<p.name> = all;
            } else if (next < loose.length()) {
                found._values<p.name> = loose<next>;
                next++;
            } else if (p.required) {
                _fail(this.program, $"missing {p.name}", this.usage());
            }
        }
        if (next < loose.length()) _fail(this.program, $"unexpected \"{loose<next>}\"", this.usage());
        return found;
    }

    // An option's value: inline (--name=x, -nx) or the next word
    func[Int] _take(o[_Option], inline[Str | Nil], words[Str<>], at[Int], found[Args], spelled[Str]) {
        if (!o.takesValue) {
            if (inline != nil) _fail(this.program, $"{spelled} doesn't take a value", this.usage());
            found._flags.add(o.name);
            return at;
        }
        if (inline != nil) {
            found._values<o.name> = inline;
            return at;
        }
        if (at >= words.length()) _fail(this.program, $"{spelled} needs a value", this.usage());
        found._values<o.name> = words<at>;
        return at + 1;
    }
}

func[Str] _pad(text[Str], width[Int]) {
    var[Str] out = text;
    while (out.length() < width) out += " ";
    return out + " ";
}
