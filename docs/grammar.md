# Grammar

The syntax of Dessa, for people working on the compiler or writing tools. To
learn the language, read [language.md](language.md) instead. Where this page and
`src/parser/parser.cpp` differ, the parser is right, and this page has a bug.

The notation is EBNF: `a?` optional, `a*` zero or more, `a+` one or more, `a | b`
either, `( )` grouping, `"x"` that text exactly.

## Tokens

Source files are UTF-8; anything else is an error.

```ebnf
identifier   = letter ( letter | digit | "_" )* ;        (* "_" counts as a letter *)
integer      = digit+ | "0x" hex+ | "0b" ("0"|"1")+ | "0o" octal+ ;
decimal      = digit+ "." digit+ ;                       (* no exponents: 1e3 isn't a literal *)
string       = '"' ... '"' ;                             (* escapes: \n \t \r \\ \" \x1b ... *)
triple       = '"""' ... '"""' ;                         (* several lines; a backslash stays a backslash *)
template     = '$"' ... "{" expression "}" ... '"' | '$"""' ... '"""' ;
char         = "'" ... "'" ;
comment      = "//" ... | "/*" ... "*/" ;
```

Reserved words:

```
var const func class interface enum   Str Int Bul Flt Dbl Chr Emp Nil
if else switch case default for while loop break continue return in
try catch throw as new this super init true false yes no nil with is
```

`Obj` is a type by its name, not a keyword. `with` is reserved for later.

## A file

```ebnf
program      = declaration* ;
declaration  = import | varDecl | constDecl | funcDecl | classDecl | enumDecl | interfaceDecl
             | annotation+ funcDecl | statement ;

import       = "$" "(" moduleSpec ( "," moduleSpec )* ","? ")" importList? ";"? ;
moduleSpec   = string ( "as" identifier )? ;
importList   = "{" importName ( "," importName )* ","? "}" ;     (* names from one module only *)
importName   = identifier ( "as" identifier )? ;

annotation   = "@" identifier ;                                  (* only @override today *)
```

## Declarations

```ebnf
varDecl      = "var" "[" type "]" declarators ";" ;
constDecl    = "const" "[" type "]" declarators ";" ;
declarators  = declarator ( "," declarator )* ;
declarator   = identifier ( "=" ( identifier "=" )* expression )? ;   (* a = b = c = 2 declares all three *)

funcDecl     = "func" "[" type throws? "]" identifier ( ":" typeParams )?
               "(" params? ")" "{" declaration* "}" ;
throws       = "!!" type ( "," type )* ;
typeParams   = identifier | "[" identifier ( "," identifier )* "]" ;
params       = param ( "," param )* ;
param        = identifier "[" type "*"? "]" ( "=" expression )? ;     (* * : the rest, last only *)

classDecl    = "class" ( "[" identifier "]" )? identifier ( ":" typeParams )?
               ( "(" qualified ( ":" typeArgs )? ")" )? "{" member* "}" ;
member       = annotation* ( varDecl | constDecl | funcDecl | constructor ) ;
constructor  = "init" "(" params? ")" "{" declaration* "}" ;

enumDecl     = "enum" identifier "{" enumValue ( "," enumValue )* ","? "}" ;
enumValue    = identifier ( "(" expression ")" )? ;

interfaceDecl = "interface" identifier "{" ( interfaceField | interfaceMethod )* "}" ;
interfaceField  = "var" "[" type "]" identifier ";" ;
interfaceMethod = "func" "[" type "]" identifier "(" params? ")" ";" ;

qualified    = identifier ( "." identifier )* ;
```

The `[Name]` after `class` says which interface it implements. The `(Parent)` is its
parent class.

## Types

```ebnf
type         = typeBase ( "<" ">" | "{" "}" )* ( "|" type )* ;   (* at most 32 suffixes *)
typeBase     = "(" type ")"
             | primitive radix?
             | "Map" "[" type "," type "]"
             | "Func" "[" type ( "," ( "[" types? "]" | type ) )? "]"
             | "Lst" "[" type "]"
             | qualified ( ":" typeArgs )? ;
primitive    = "Str" | "Int" | "Bul" | "Flt" | "Dbl" | "Chr" | "Emp" | "Nil" ;
radix        = "[" ( "2" | "8" | "10" | "16" ) "]" ;             (* Int only: how it prints *)
typeArgs     = type | "[" types "]" ;
types        = type ( "," type )* ;
```

`T<>` is a list, `T{}` a set, and `A | B` a union. A list of a union needs brackets:
`(Int | Str)<>`. `Lst[T]` is another spelling of `T<>`.

## Statements

```ebnf
statement    = "if" "(" expression ")" body ( "else" ( ifRest | body ) )?
             | "switch" "(" expression ")" "{" switchCase* "}"
             | "for" "(" forClauses ")" body
             | "while" "(" expression ")" body
             | "loop" "{" declaration* "}"
             | "return" expression? ";"
             | "break" ";"
             | "continue" ";"
             | "try" "{" declaration* "}" catchArm+
             | "throw" expression ";"
             | "{" declaration* "}"
             | expression ";" ;

body         = "{" declaration* "}" | statement ;
switchCase   = "case" expression ":=" "{" declaration* "}"
             | "default" ":=" "{" declaration* "}" ;
forClauses   = "var" "[" type "]" identifier "in" expression
             | ( varInit | statement | ";" ) expression? ";" expression? ;
varInit      = "var" "[" type "]" identifier ( "=" expression )? ";" ;
catchArm     = "catch" ( "(" identifier "[" type "]" ")" )? "{" declaration* "}" ;
```

A body's `...` is a placeholder (in declaration files), with an optional `;`.
Dessa has no `do`/`while` and no `finally`, on purpose.

## Expressions

From the loosest to the tightest:

| Level | Operators | Groups |
|---|---|---|
| assignment | `=` `+=` `-=` `*=` `/=` `^=` `%=` `?=` | right |
| ternary | `? :` | right |
| pipe | `\|>` | left |
| nil | `??` `?!` | left |
| or | `\|\|` | left |
| and | `&&` | left |
| bit or | `\|` | left |
| bit xor | `^^` | left |
| bit and | `&` | left |
| equality | `==` `!=` | left |
| comparison | `<` `>` `<=` `>=` `is` | can't be chained |
| shift | `<<` `>>` | left |
| addition | `+` `-` | left |
| multiplication | `*` `/` `%` | left |
| unary | `!` `-` `~` | right |
| exponent | `^` | right; `-2 ^ 2` is `-(2 ^ 2)` |
| postfix | `++` `--` | |
| call | `f(...)` `a.b` `a?.b` `a<i>` | left |

```ebnf
call         = primary ( "(" arguments? ")"
                       | ( "." | "?." ) ( identifier | "init" ) ( ":" typeArgs )?
                       | "<" addition ">" )* ;
arguments    = argument ( "," argument )* ","? ;
argument     = ( identifier ":" )? "..."? expression ;

primary      = literal | template | identifier ( ":" typeArgs )? | "this" | "super"
             | conversion | newExpr | list | oldList | mapOrSet | lambda | "(" expression ")" ;
literal      = integer | decimal | string | triple | char | "true" | "false" | "yes" | "no" | "nil" | "..." ;
conversion   = ( "Str" | "Int" | "Bul" | "Flt" | "Dbl" | "Chr" ) "(" expression ")" ;

newExpr      = "new" qualified ( ":" typeArgs )? "(" arguments? ")"
             | "new" "{" ( anonField ( ( "," | ";" ) anonField )* ( "," | ";" )? )? "}" ;
anonField    = ( "var" "[" type "]" )? identifier ( "=" | ":" ) expression ;

list         = "<" ( item ( "," item )* ","? )? ">" ;
oldList      = "[" ( item ( "," item )* ","? )? "]" ;     (* still compiles, with a warning *)
item         = "..."? expression ;
mapOrSet     = "{" ":" "}"                                (* an empty map *)
             | "{" "}"                                    (* an empty map or set, by context *)
             | "{" item ( "," item )* ","? "}"            (* a set *)
             | "{" entry ( "," entry )* ","? "}" ;        (* a map *)
entry        = expression ":" expression | "..." expression ;

lambda       = "(" lambdaParams? ")" ":=" ( "{" declaration* "}" | expression ) ;
lambdaParams = identifier "[" type "]" ( "," identifier "[" type "]" )* ;
```

## What the grammar alone can't decide

**`<` is three things**: a comparison, an index and the start of a list.

- `a<i>` is tried as an index: after a call-level expression, the parser tries
  `<` addition `>`. If that doesn't fit, it rewinds, and `<` is a comparison. Failed
  attempts are remembered (`Parser::notAnIndex`), so nesting can't make this exponential.
- A `<` where an expression starts is a list.
- Inside a list, the first `>` closes it, so a comparison there needs brackets:
  `<(a > b)>`. `<1 > 2>` gets an error that says so.
- `>>` and `<<` are split into two tokens where lists or indexes open or close:
  `m<xs<0>>`, `<<1>, <2>>`.

**`:`** is a generic's types only where one can fit: `pick:Int(items)` needs a `(`
after the types. In a ternary's then-branch or a map's key a `:` ends the expression.
`f(x: 1)` with spaces, or with a value that isn't a type, is a named argument.

**`{` after `$(...)`** is the import's list of names when a name follows it;
otherwise the statement has ended, as the `;` is optional there.

## Limits

| What | Limit | Why |
|---|---|---|
| Nesting (`Parser::kMaxNesting`) | 200 levels | The parser, analyser and generators recurse |
| Chains of operators, of calls, of `\|>` and of `else if` (`kMaxChain`) | 1000 | They make trees as deep, without nesting |
| Suffixes on a type (`kMaxTypeSuffixes`) | 32 | The same, for types |
