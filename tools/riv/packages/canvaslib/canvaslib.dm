/***
 * $("canvaslib") -- drawing and simple games on a web page.
 *
 *     <script>
 *     $("canvaslib") { Canvas, rgb }
 *
 *     var[Canvas] c = new Canvas(400, 300);       // a <canvas> at the end of the page
 *     var[Dbl] x = 20;
 *     c.onFrame((dt[Dbl]) := {                    // about 60 times a second; dt = seconds since the last
 *         if (c.isKeyDown("ArrowRight")) x += 120 * dt;
 *         if (c.isKeyDown("ArrowLeft")) x -= 120 * dt;
 *         c.background("#203040");
 *         c.fill("tomato");
 *         c.circle(x, 150, 20);
 *         c.fill("white");
 *         c.text($"x = {x.toFixed(0)}", 10, 24);
 *     });
 *     </script>
 *
 * Shapes (rect, circle, line, polygon, text, image) use the colour set by
 * fill(...) for the inside or stroke(...) for outlines and lines. Colours are
 * CSS colours: "red", "#ff8800", rgb(0, 128, 255).
 *
 * Input: c.mouseX / c.mouseY (inside the canvas), c.mouseDown,
 * c.isKeyDown("a") / ("ArrowUp") / (" "), onClick and onKey for single events.
 *
 * Pages only (a .dhtml file): it draws through $("webutils"). c.context is the
 * browser's own drawing object for everything else (rotate, measureText, ...).
 */

$("webutils") { document, Element, Context2D, Event, setInterval, clearTimer };

const[Dbl] _TURN = 6.283185307179586;   // a whole circle, in radians

class Canvas {
    var[Element] element;          // the <canvas>
    var[Context2D] context;        // its drawing surface (the browser's own)
    var[Dbl] width = 0;
    var[Dbl] height = 0;
    var[Dbl] mouseX = 0;
    var[Dbl] mouseY = 0;
    var[Bul] mouseDown = false;
    var[Int] frame = 0;            // how many frames onFrame has drawn
    var[Str{}] _keys = {};
    var[Map[Str, Element]] _images = {:};
    var[Int] _timer = -1;

    // A new canvas inside the element `parent` (a CSS selector such as "#game"),
    // or at the end of the page when it's "" or not found
    init(width[Dbl] = 400, height[Dbl] = 300, parent[Str] = "") {
        this.width = width;
        this.height = height;
        this.element = document.createElement("canvas");
        this.element.width = width;
        this.element.height = height;
        var[Element | Nil] holder = parent == "" ? nil : document.querySelector(parent);
        if (holder != nil) holder.appendChild(this.element);
        else document.body.appendChild(this.element);
        this.context = this.element.getContext("2d");
        this.context.textBaseline = "alphabetic";
        this.element.addEventListener("mousemove", (e[Event]) := { this._mouse(e); });
        this.element.addEventListener("mousedown", (e[Event]) := {
            this._mouse(e);
            this.mouseDown = true;
        });
        document.addEventListener("mouseup", (e[Event]) := { this.mouseDown = false; });
        document.addEventListener("keydown", (e[Event]) := {
            this._keys.add(e.key);
            // arrows and space would scroll the page
            if (e.key.startsWith("Arrow") || e.key == " ") e.preventDefault();
        });
        document.addEventListener("keyup", (e[Event]) := { this._keys.remove(e.key); });
    }

    func[Emp] _mouse(e[Event]) {
        var[Dbl] left = this.element.getBoundingClientRect().left;
        var[Dbl] top = this.element.getBoundingClientRect().top;
        this.mouseX = e.clientX - left;
        this.mouseY = e.clientY - top;
    }

    // ---- colours and lines

    // The colour shapes and text are filled with
    func[Emp] fill(color[Str]) {
        this.context.fillStyle = color;
    }

    // The colour (and width) outlines and lines are drawn with
    func[Emp] stroke(color[Str], width[Dbl] = 1) {
        this.context.strokeStyle = color;
        this.context.lineWidth = width;
    }

    // 0 (see-through) to 1 (solid), for everything drawn after it
    func[Emp] alpha(value[Dbl]) {
        this.context.globalAlpha = value;
    }

    // ---- shapes

    // Everything erased (see-through)
    func[Emp] clear() {
        this.context.clearRect(0, 0, this.width, this.height);
    }

    // The whole canvas in one colour (the fill colour stays as it was)
    func[Emp] background(color[Str]) {
        var[Str] before = this.context.fillStyle;
        this.context.fillStyle = color;
        this.context.fillRect(0, 0, this.width, this.height);
        this.context.fillStyle = before;
    }

    func[Emp] rect(x[Dbl], y[Dbl], width[Dbl], height[Dbl]) {
        this.context.fillRect(x, y, width, height);
    }

    func[Emp] rectOutline(x[Dbl], y[Dbl], width[Dbl], height[Dbl]) {
        this.context.strokeRect(x, y, width, height);
    }

    // x, y is the centre
    func[Emp] circle(x[Dbl], y[Dbl], radius[Dbl]) {
        this.context.beginPath();
        this.context.arc(x, y, radius, 0, _TURN);
        this.context.fill();
    }

    func[Emp] circleOutline(x[Dbl], y[Dbl], radius[Dbl]) {
        this.context.beginPath();
        this.context.arc(x, y, radius, 0, _TURN);
        this.context.stroke();
    }

    func[Emp] line(x1[Dbl], y1[Dbl], x2[Dbl], y2[Dbl]) {
        this.context.beginPath();
        this.context.moveTo(x1, y1);
        this.context.lineTo(x2, y2);
        this.context.stroke();
    }

    // A filled shape through the points <x1, y1, x2, y2, ...>
    func[Emp] polygon(points[Dbl<>]) {
        if (points.length() < 4) return;
        this.context.beginPath();
        this.context.moveTo(points<0>, points<1>);
        for (var[Int] i = 2; i + 1 < points.length(); i += 2) this.context.lineTo(points<i>, points<i + 1>);
        this.context.closePath();
        this.context.fill();
    }

    // Text with its left end at x and its baseline at y
    func[Emp] text(words[Str], x[Dbl], y[Dbl], size[Dbl] = 16, font[Str] = "sans-serif") {
        this.context.font = $"{size}px {font}";
        this.context.fillText(words, x, y);
    }

    func[Dbl] textWidth(words[Str], size[Dbl] = 16, font[Str] = "sans-serif") {
        this.context.font = $"{size}px {font}";
        return this.context.measureText(words).width;
    }

    // A picture from an address ("player.png"); drawn once it has loaded
    func[Emp] image(url[Str], x[Dbl], y[Dbl]) {
        if (!this._images.has(url)) {
            var[Element] img = document.createElement("img");
            img.src = url;
            this._images<url> = img;
        }
        var[Element] img = this._images<url>;
        if (img.complete) this.context.drawImage(img, x, y);
    }

    // ---- input

    // Is the key held down now? Names as the browser gives them: "a", "A",
    // " ", "Enter", "ArrowLeft", "Shift", ...
    func[Bul] isKeyDown(key[Str]) {
        return this._keys.includes(key);
    }

    // Runs `handler(x, y)` when the canvas is clicked
    func[Emp] onClick(handler[Func[Emp, [Dbl, Dbl]]]) {
        this.element.addEventListener("click", (e[Event]) := {
            this._mouse(e);
            handler(this.mouseX, this.mouseY);
        });
    }

    // Runs `handler(key)` each time a key is pressed
    func[Emp] onKey(handler[Func[Emp, [Str]]]) {
        document.addEventListener("keydown", (e[Event]) := { handler(e.key); });
    }

    // ---- animation

    // Runs `draw(dt)` `fps` times a second (dt: seconds per frame) until stop()
    func[Emp] onFrame(draw[Func[Emp, [Dbl]]], fps[Int] = 60) {
        this.stop();
        const[Dbl] dt = 1.0 / fps;
        this._timer = setInterval(() := {
            this.frame++;
            draw(dt);
        }, Int(1000 / fps));
    }

    func[Emp] stop() {
        if (this._timer >= 0) clearTimer(this._timer);
        this._timer = -1;
    }
}

// A colour from red, green, blue (0-255) and alpha (0-1): rgb(255, 128, 0)
func[Str] rgb(red[Int], green[Int], blue[Int], alpha[Dbl] = 1) {
    return $"rgb({red} {green} {blue} / {alpha})";
}
