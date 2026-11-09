# json

Read and write JSON: as a `Json` value, or straight into your own classes.

**Install:** Install with `riv add json`.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("json");

class User {
    var[Str] name = "";
    var[Int] age = 0;
    var[Str<>] tags = <>;
}

var[Json] data = parseJson("{\"name\": \"Ann\", \"tags\": [1, 2]}");
print(data.get("name").asStr(), data.get("tags").at(1).asInt());
print(stringify(data, 2));       // indented

var[User] ann = parseJsonAs:User("{\"name\": \"Ann\", \"age\": 31}");
var[User<>] all = parseJsonListAs:User("[{\"name\": \"Bo\"}, {\"name\": \"Cy\"}]");
print(toJson(ann), all.length());
```

## Into your classes

Each field gets the key with the same name (case and `_` don't matter). The class
needs an `init()` without arguments. Fields that are `Str`, `Int`, `Dbl`, `Flt`,
`Bul`, `Json` or lists of `Str`, `Int`, `Dbl`, `Bul` are filled; a value that doesn't
fit its field is a `JsonError` naming the key.

## What it has

| Part | Names |
|---|---|
| Reading | `parseJson(text, maxDepth)`, `parseJsonAs:T`, `parseJsonListAs:T`, `parseJsonLines` |
| Writing | `stringify(value, indent)`, `toJson(object)`, `toJsonLines` |
| Inside a value | `get`, `at`, `path("user.tags.0")`, `asStr`, `asInt`, `asBig`, `intOr(key, fallback)`, `to:T()`, `toList:T()`, `arrayItems()` |
| Changing | `merge(changes)` (objects, deeply) |
| Comparing | `==` (the same value, in any key order) |
| Checking a shape | `new JsonSchema(text)`: `check(value)` lists the problems, `require(value)` throws them |

`JsonSchema` knows this much of JSON Schema: `type`, `required`, `properties`,
`additionalProperties: false`, `items`, `enum`, `minimum`, `maximum`, `minLength`,
`maxLength`, `minItems`, `maxItems`.

## Good to know

Numbers keep their exact text, so `asInt()` and `asBig()` give every digit back.

Nesting past 512 levels is a `JsonError` (`parseJson(text, maxDepth)` changes that),
so text from elsewhere can't use up the stack.
