// A .dm module may only contain declarations
const[Int] LIMIT = 3;
func[Int] twice(x[Int]) {
    return x * 2;
}
print("not allowed here");     // error: can only contain declarations
