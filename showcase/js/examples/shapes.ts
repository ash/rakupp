// Polymorphism through an interface and a small class hierarchy.
interface Shape {
  area(): number;
  perimeter(): number;
  kind(): string;
}

class Rectangle {
  constructor(protected w: number, protected h: number) {}
  area(): number { return this.w * this.h; }
  perimeter(): number { return 2 * (this.w + this.h); }
  kind(): string { return "rectangle"; }
}

// A square is a rectangle whose constructor fixes both sides equal,
// and which relabels itself — the area/perimeter come free from the base.
class Square extends Rectangle {
  constructor(side: number) { super(side, side); }
  kind(): string { return "square"; }
}

class Circle {
  constructor(private r: number) {}
  area(): number { return Math.PI * this.r * this.r; }
  perimeter(): number { return 2 * Math.PI * this.r; }
  kind(): string { return "circle"; }
}

const shapes: Shape[] = [
  new Rectangle(3, 4),
  new Square(2),
  new Circle(5),
];

for (const s of shapes) {
  console.log(
    s.kind().padEnd(10),
    "area =", s.area().toFixed(2),
    " perimeter =", s.perimeter().toFixed(2),
  );
}

const totalArea = shapes.reduce((sum, s) => sum + s.area(), 0);
const biggest = shapes.reduce((best, s) => (s.area() > best.area() ? s : best));
console.log("total area:", totalArea.toFixed(2));
console.log("biggest:", biggest.kind());
