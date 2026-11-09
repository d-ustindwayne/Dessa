# canvaslib

Drawing and simple games in a page.

**Install:** Install with `riv add canvaslib`.

**Runs in:** Web pages only (`.dhtml`).

## Example

```dessa
$("canvaslib") { Canvas, rgb }

var[Canvas] c = new Canvas(400, 300);
var[Dbl] x = 20;
c.onFrame((dt[Dbl]) := {
    if (c.isKeyDown("ArrowRight")) x += 120 * dt;
    c.background("#203040");
    c.fill("tomato");
    c.circle(x, 150, 20);
});
```

## What it has

| Part | Names |
|---|---|
| Shapes | `rect`, `rectOutline`, `circle`, `circleOutline`, `line`, `polygon`, `text`, `image(url, x, y)` |
| Colours | `fill`, `stroke(color, width)`, `alpha`, `background`, `clear`, `rgb` |
| Input | `mouseX`, `mouseY`, `mouseDown`, `isKeyDown(key)`, `onClick`, `onKey` |
| Animation | `onFrame(draw, fps)`, `stop()` |
| The browser's own | `c.context`, a `Context2D` |
