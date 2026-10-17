/***
 * geometry.dm -- a module file.
 *
 * .dm files hold declarations (classes, functions, constants) for other
 * files to import. They compile exactly like .ds files for now.
 */

const[Dbl] PI = 3.141592653589793;

interface Shape {
    func[Dbl] area();
    func[Str] name();
}

class[Shape] Circle {
    var[Dbl] radius = 1.0;
    init(radius[Dbl]) {
        self.radius = radius;
    }
    func[Dbl] area() {
        return PI * self.radius ^ 2;
    }
    func[Str] name() {
        return "circle";
    }
}

class[Shape] Rectangle {
    var[Dbl] width = 1.0;
    var[Dbl] height = 1.0;
    init(width[Dbl], height[Dbl]=1.0) {
        self.width = width;
        self.height = height;
    }
    func[Dbl] area() {
        return self.width * self.height;
    }
    func[Str] name() {
        return self.width == self.height ? "square" : "rectangle";
    }
}

func[Dbl] totalArea(shapes[Shape*]) {
    var[Dbl] total = 0.0;
    for (var[Shape] s in shapes) {
        total += s.area();
    }
    return total;
}
