# xml

Read and write XML.

**Install:** Comes with Dessa, nothing to install.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```
$("xml") { Xml, parseXml, XmlError }

var[Xml] doc = parseXml("<catalog><book id=\"1\">Dune</book></catalog>");
print(doc.find("book")?.attr("id"));     // 1
print(doc.find("book")?.text());         // Dune
print(doc.pretty());
```

In an `.xds` file it can also be written as markup:

```
var[Xml] data = @(<catalog><book id={id}>{title}</book></catalog>);
```

## What it has

| Name | Gives |
|---|---|
| `parseXml(text)` | text to an `Xml`; an `XmlError` says the line and column of what's wrong |
| `new Xml(name)` | a new element |
| `x.name`, `x.attributes`, `x.nodes` | its name, attributes, and what's inside (`Xml` and `Str`) |
| `x.attr(name)` | an attribute's value, or nil |
| `x.setAttr(name, value)`, `x.append(node)` | change it (they return `x`) |
| `x.children()` | the elements directly inside |
| `x.find(name)`, `x.findAll(name)` | elements with this name, at any depth |
| `x.text()` | all the text inside |
| `x.toStr()`, `x.pretty()` | back to XML, on one line or indented |

## Safety

Values are escaped when written out. A DOCTYPE is read past and never understood: its entities aren't
expanded and nothing is fetched, which rules out XXE and entity-expansion attacks. Elements can nest
256 deep; past that it's an `XmlError`, not a crash.
