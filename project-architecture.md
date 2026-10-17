# File Tree: Dessa

**Last Updated:** 16/10/2026
**Generated:** 21/08/2026, 13:08:05
**Root Path:** `/`

```
├── 📄 CMakeLists.txt              build: cmake -S . -B build && cmake --build build
├── 📁 runtime                     C++ support library used by generated code
│   ├── ⚡ dessa_runtime.hpp        core types, print, errors, nil helpers, builtin methods
│   └── 📁 lib                     builtin libraries, imported with $(<name>)
│       ├── ⚡ math.hpp
│       ├── ⚡ os.hpp
│       ├── ⚡ sys.hpp
│       └── ⚡ time.hpp
├── 📁 sample
│   ├── 📄 sample.ds               language spec / tour (runs end to end)
│   ├── 📄 sample.phl
│   ├── 📄 test_control.ds
│   ├── 📄 test_oop.ds
│   ├── 📄 test_operators_keywords.ds
│   └── 📁 my                      modules imported by sample.ds
│       ├── 📄 other.ds
│       └── 📁 other
│           └── 📄 directory.ds
├── 📁 src
│   ├── ⚡ main.cpp                 CLI: check / emit / build / run / symbols / tokens / ast
│   ├── 📁 common
│   │   └── ⚡ program.hpp          Module, Program, Diagnostic
│   ├── 📁 lexer
│   │   ├── ⚡ lexer.cpp
│   │   ├── ⚡ lexer.hpp
│   │   └── ⚡ token.hpp
│   ├── 📁 parser
│   │   ├── ⚡ ast.hpp
│   │   ├── ⚡ parser.cpp
│   │   └── ⚡ parser.hpp
│   ├── 📁 driver
│   │   ├── ⚡ loader.cpp/.hpp      reads the entry file + every imported .ds file
│   │   ├── ⚡ toolchain.cpp/.hpp   finds g++ and runtime/, compiles (cached), runs
│   │   └── ⚡ json.hpp
│   ├── 📁 analyser
│   │   ├── ⚡ analyser.cpp         modules, declarations, statements
│   │   ├── ⚡ analyser_expr.cpp    expressions, calls, members, lambdas
│   │   ├── ⚡ analyser.hpp
│   │   ├── ⚡ builtins.cpp/.hpp    builtin functions + library registry
│   │   ├── ⚡ scope.cpp/.hpp       symbols, scopes, nil-narrowing
│   │   └── ⚡ semtype.cpp/.hpp     semantic types
│   └── 📁 codegen
│       ├── ⚡ codegen.cpp          analysed AST -> one C++20 file
│       └── ⚡ codegen.hpp
├── 📁 tests
│   ├── 🐍 test.py                 python tests/test.py [--update] [filter]
│   └── 📁 cases
│       ├── 📁 errors              inline `// error: ...` expectations
│       └── 📁 run                 programs + expected .out
├── 📁 tools
│   └── 📁 vscode-dessa            VS Code extension (plain JS, no build step)
└── 📝 README.md
```
