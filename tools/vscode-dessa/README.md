# Dessa for VS Code

Support for the Dessa language: `.ds` files (and `.dm`, `.dx`, ...), `.dhtml`
pages, and Dessa scripts inside HTML pages.

- Highlighting, also for `<script type="text/dessa">` in HTML and for `.dhtml` pages
- Errors as you type, from the Dessa compiler itself
- Completion from the real types (`win.` lists the members of win's class)
- Hover with the type, the documentation and a link to where it is defined
- Go to definition (F12 or ctrl+click), links on imports such as `$(<webutils>)`
- Parameter hints while typing a call
- HTML help in `.dhtml` pages: tag and attribute completion and hover, matching tags, folding
- Run (Ctrl+F5) for `.ds` files and `.dhtml` pages

It needs the `dessa` compiler. Set `dessa.compilerPath` if `dessa` isn't on
your PATH.

Full description, settings and how to install by hand:
[docs/editor.md](../../docs/editor.md).
