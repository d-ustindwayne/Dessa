# Samples

Read them in order: each one uses what came before it. Run any of them with `dessa run`:

```
dessa run sample/fundamentals/01_hello.ds
```

## Fundamentals

| File | Shows |
|---|---|
| [01_hello](fundamentals/01_hello.ds) | printing |
| [02_values](fundamentals/02_values.ds) | variables, constants, the types, nil |
| [03_text](fundamentals/03_text.ds) | text, template strings, characters |
| [04_choices_and_loops](fundamentals/04_choices_and_loops.ds) | `if`, `switch`, `for`, `while`, `loop` |
| [05_collections](fundamentals/05_collections.ds) | lists, sets, maps |
| [06_functions](fundamentals/06_functions.ds) | functions, defaults, named arguments, lambdas, `\|>` |

## Objects

| File | Shows |
|---|---|
| [07_classes](oop/07_classes.ds) | fields, `init`, methods, `toStr` |
| [08_inheritance](oop/08_inheritance.ds) | parents, `super`, `@override`, `is` |
| [09_interfaces_and_enums](oop/09_interfaces_and_enums.ds) | interfaces, enums |
| [10_generics](oop/10_generics.ds) | generic classes and functions |
| [11_errors](oop/11_errors.ds) | `try`, `catch`, your own errors, `!!` |
| [12_special_methods](oop/12_special_methods.ds) | `_add`, `_eq`, `_get`, `_iter`, ... |

## Libraries

| File | Shows |
|---|---|
| [13_math](libraries/13_math.ds) | `math`, `Big` |
| [14_files](libraries/14_files.ds) | `os`: files, also in other encodings |
| [15_dates](libraries/15_dates.ds) | `time`: dates and time zones |
| [16_data_structures](libraries/16_data_structures.ds) | `dsds` |
| [17_json](libraries/17_json.ds) | the `json` package (`riv add json`) |
| [18_xml](libraries/18_xml.xds) | `xml`, and XML written as markup |
| [19_async](libraries/19_async.ds) | `async`, `await`, `Task` |

## On the web

| File | Shows |
|---|---|
| [web/counter.dhtml](web/counter.dhtml) | a page with a Dessa script: `dessa run sample/web/counter.dhtml` |
| [web/page.html](web/page.html) and [web/app.xds](web/app.xds) | components: `dessa run sample/web/page.html` |

## Bigger

- [bank.ds](bank.ds): a whole small program
- [sample.ds](sample.ds): the whole language on one page, as a reference
- [filetypes/](filetypes): modules, across the kinds of Dessa files
