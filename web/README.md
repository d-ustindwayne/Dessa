# web

`example/counter.dhtml` is a web page written in Dessa: a plain `<script>`
in a `.dhtml` file is Dessa, compiled to WebAssembly.

```
dessa run web/example/counter.dhtml      opens it in the browser
dessa build web/example/counter.dhtml    writes counter.html, one file that runs anywhere
```

How it all works: [docs/web.md](../docs/web.md).
