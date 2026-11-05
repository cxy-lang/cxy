# Memory Management

Cxy has no garbage collector and no `free`. Memory is released at a **known
point in your code**: when the last variable that owns an object goes out of
scope. This chapter explains the rules, how to write code that follows them, what
the compiler tells you when you don't, and how to find leaks in a running program.

If you only remember one thing: **a class variable is an owner. When the last
owner goes away, the object is destroyed.**

## Values and objects

Cxy has two kinds of types:

| Kind | Examples | What a variable holds |
| --- | --- | --- |
| **Value types** | numbers, `bool`, `char`, tuples, `struct`s | the data itself |
| **Classes** | every `class` | a handle to an object on the heap |

Copying a value copies the data. Copying a class variable does **not** copy the
object, it adds another owner of the same object. The object keeps a count of its
owners and is destroyed when the count reaches zero.

```cxy
class Node {
    id: i32
    func `init`(id: i32) { this.id = id }
    func `deinit`() { println("node ", this.id, " destroyed") }
}

func scope() {
    var a = Node(1)
    var b = Node(2)
    println("end of scope")
}
```

```
end of scope
node 2 destroyed
node 1 destroyed
```

`deinit` runs when the object is destroyed, then the object's own fields are
released. Variables are released in **reverse order of declaration**, when their
scope ends. Assigning to a variable releases the old object first:

```cxy
var a = Node(40)
a = Node(41)          // node 40 is destroyed here
```

A `struct` that has class fields owns them: when the struct goes away, so do its
references.

## Several owners: copying

`var b = a` makes `b` a second owner of the object `a` refers to. Nothing is
copied:

```cxy
func shared() {
    var a = Node(10)
    var b = a                 // same object, two owners
    println("a and b: ", a.id, " ", b.id)
}                             // destroyed once, after both are gone
```

When you want a **new object** with the same contents, ask for one. Types that
support it have a `clone()` method:

```cxy
var s = "hello".S
var t = s.clone()             // a new String
s << " world"
println(s.str(), " / ", t.str())      // hello world / hello
```

| You write | You get |
| --- | --- |
| `var b = a` | a second owner of the same object |
| `a.clone()` | a new object with the same contents |

## Moving: `&&`

`&&x` hands your ownership to someone else. The object is not touched and no count
changes, but `x` can no longer be used:

```cxy
func take(n: Node): i32 => n.id

func moving() {
    var a = Node(20)
    println(take(&&a))        // a's ownership goes into take
    println(a.id)             // warning: `a` is used after it was moved
}
```

Prefer a move when you are done with a variable: it is free, while a copy costs a
count change. Moving works out of fields and into containers too:

```cxy
var v = Vector[Node]()
v.push(Node(30))              // a temporary moves into the vector
v.push(&&named)               // so does a variable you give up
var n = &&holder.node         // takes the field, leaving it empty
```

Returning a local variable moves it, you never write `&&` there:

```cxy
func make(): Node {
    var n = Node(1)
    return n                  // n moves to the caller
}
```

## Borrowing: `&T`

A parameter of type `&T` **borrows** the object. The caller keeps ownership, no
count changes and nothing can be destroyed by the call:

```cxy
func peek(n: &Node): i32 => n.id
func take(n: Node): i32 => n.id

func passing() {
    var a = Node(20)
    peek(a)                   // borrow
    take(a)                   // a copy goes in, a is still valid
    take(&&a)                 // a goes in, a is dead afterwards
}
```

The rule for parameters is simple:

- A parameter that is **not** a reference is **owned by the function**. It is
  released when the function returns, unless the function moves it, stores it or
  returns it.
- A `&T` parameter is borrowed.
- A return value is always owned by the caller.

So make a parameter `&T` when the function only **reads** the object. That is the
common case, and it avoids a count change on every call. Keep `T` when the
function keeps the object (stores it in a field, returns it, passes it to another
thread).

## From a reference to an owner: `@copy`

A `&T` is not an owner, so it cannot be stored where an owner is expected. The
compiler does not guess, it asks you to write `@copy`:

```cxy
func keep(r: &Node): Node {
    var n: Node = r           // error
    return n
}
```

```
error: variable initializer of type '&class Node' is not assignable to variable type 'class Node'
note: a reference is not an owner, write `@copy x` to take an owner of the referenced value
```

```cxy
func keep(r: &Node): Node {
    var n = @copy r           // an owner, the object stays alive as long as n does
    return n
}
```

`@copy x` always means "a new owner of the object `x` refers to". On a variable
that already owns the object it is allowed but not needed.

| Written | Type of the result |
| --- | --- |
| `var b = a` (`a` an owner) | owner |
| `var k = &a` | reference |
| `var l = @copy k` (`k` a reference) | owner |

## Temporaries live until the end of the statement

A value that is not stored anywhere (a function result you only read from) is
destroyed when the statement that made it ends:

```cxy
println(make().id)            // the Node is destroyed after this line
```

This matters when you keep something that points into it:

```cxy
var v = f().view()            // bug: f()'s result is gone, v points into it
var s = f()                   // fine: s owns it
var v = s.view()
```

## Borrows can dangle

A `&T` carries no count, so nothing stops its owner from going away while the
borrow is still in use. The compiler does not prove that this cannot happen, it is
your responsibility:

```cxy
class View {
    src: &Node
    func `init`(n: &Node) { this.src = n }
    func read(): i32 => src.id
}

func main() {
    var n = Node(1)
    var v = View(&n)
    println(v.read())         // 1
    n = Node(2)               // the first node is destroyed here
    println(v.read())         // the borrow is dangling
}
```

Rules of thumb:

- A `&T` is valid until its owner is reassigned, moved, cleared or destroyed.
- To keep a value across an operation that might do that, take an owner:
  `var f = @copy c.first()`.
- A `&T` field means "the target outlives me". Use a weak reference when that
  cannot be guaranteed.
- Never store a borrow of a local variable in an object that lives longer than the
  variable.

Debug builds (`-g`, which `cxy dev` and `--memory-trace` use) catch many of these:
using a `&T` that was read from a field, or returned by a call, after its object
was destroyed stops the program with a message that names the line:

```
panic: borrowed reference used after its owner was destroyed
  @ dangling.cxy:8:0
```

Release builds do not check, so the rule above is what keeps you safe there. The
memory trace (see below) can list the dangling borrows of a running program.

## Cycles and weak references

Counting has one weakness: two objects that own each other are never released,
because each one keeps the other's count above zero.

```cxy
func leakCycle() {
    var a = Node(1)
    var b = Node(2)
    a.next = @copy b
    b.next = @copy a
}                             // both objects are leaked
```

The fix is a **weak reference**, which points at an object without keeping it
alive. It is written `Weak[T]`, or `@weak T`, and `T` must be a class:

```cxy
class Parent {
    child: Child
    func `init`() {}
    func `deinit`() { println("parent gone") }
}

class Child {
    parent: @weak Parent                       // does not keep the parent alive
    func `init`(p: &Parent) { this.parent = Weak[Parent](p) }
    func `deinit`() { println("child gone") }
}

func main() {
    var p = Parent()
    p.child = Child(p)
    var up = p.child.parent.upgrade()          // Parent?
    if (up) {
        println("the parent is still alive")
    }
}
```

| Operation | Meaning |
| --- | --- |
| `Weak[T](obj)` | a weak reference to `obj` (`obj` can be an owner or a `&T`) |
| `Weak[T]()` | a weak reference to nothing |
| `w.upgrade()` | a `T?`: a new owner while the object is alive, empty once the last owner is gone |
| `@copy w` | a second weak reference to the same object |

When the last owner of an object goes away the object is destroyed, `deinit` runs
and its fields are released, even if weak references are left. Only the object's
memory block stays around until the last weak reference is gone (the trace calls
such a block a *zombie*). `upgrade()` on it gives an empty optional.

Use a weak reference for parent pointers, observers and caches, anywhere the
relation is "I know about you, but you don't depend on me". `Weak[T]` works in
every build, also with `--without-builtins`.

## Types that must not be copied

Some things must have exactly one owner: a mutex, a socket, a file handle. Mark
the type `@nocopy` and the compiler rejects every copy of it. It can still be
moved and borrowed:

```cxy
@nocopy
class Handle {
    fd: i32
    func `init`(fd: i32) { this.fd = fd }
    func `deinit`() { /* close the file */ }
}

var a = Handle(1)
var b = a                     // error: `Handle` cannot be copied
var c = &&a                   // fine
peek(&c)                      // fine: a borrow
```

A struct or class that contains a `@nocopy` value is `@nocopy` too, and so are the
classes that inherit from one.

## Threads

Counts are atomic, so a class object can be shared between threads (and
coroutines) and is still destroyed exactly once. That only protects the counts: if
several threads change the **contents** of an object, you need a lock or atomic
fields as usual.

## What the compiler checks

The compiler can verify your ownership at compile time. The checks are warnings
that are off by default, switch them on with `--warnings=Ownership`:

```
cxy build --warnings=Ownership app.cxy
```

```
warning[UseAfterMove]: `a` is used after it was moved or dropped (in main)
  in move.cxy:9:13
  |
9 |    println(a.id)
  |            ^
note: moved here
  in move.cxy:8:10
  |
8 |    take(&&a)
  |         ^^^
```

The name in the brackets is what you pass to `--warnings` to turn one off
(`--warnings='Ownership|~DanglingBorrow'`), and to `--warning-as-error` to make it
fail the build.

Warnings about your code:

| Warning | What it means |
| --- | --- |
| `UseAfterMove` | a variable is used after `&&x` gave it away, or after it was dropped |
| `MoveOfReference` | `&&r` on a reference: you cannot give away what you do not own, write `@copy r` |
| `ReturnedBorrow` | a borrow of a local variable is returned, but the local dies at the end of the function |
| `DanglingBorrow` | a reference is read after its owner was moved or dropped |
| `MovedGlobal` | a function moves a global out and does not put a value back before it returns |
| `OwnerLeak` | an owner is still held when the function exits and nothing releases it |

The remaining warnings (`DoubleDrop`, `DropFlagMismatch`, `UnclearedMove`,
`OverwrittenOwner`, `RedeclaredOwner`, `TemporaryOwnership`) check the code the
compiler generated itself. If one appears it is most likely a compiler bug, please
report it with the program that triggered it.

The ownership **errors** need no switch: copying a reference into an owner without
`@copy`, and copying a `@nocopy` type, always stop the build.

## Finding leaks and memory problems

A leak is an object that nobody can reach any more but that is still alive. Cxy
has a built-in tracer for finding them. It is a special build of your program that
records every object and every container buffer, and a tool that asks the running
program questions.

### Building a traced program

Add `--memory-trace` to the build:

```
cxy build --memory-trace -o app app.cxy
```

This implies `-g`, links the trace library (`libcxy-trace`, installed with the
compiler; set `CXY_MEMORY_TRACE_LIB_DIR` if it is somewhere else) and makes the
program a little slower and bigger. Use it for debugging, not for releases.

### The exit report

Run the program. When it ends, it prints every object that is still alive:

```
=== CXY Memory Leak Summary ===
[LEAK] ptr=0x7bc6c04d10 size=16 refs=1 @ app.cxy:21  <- cycle of 2 nodes, held by 0x7bc6c04cb0
  Allocation callstack:
    #0  Node_op__initCover (in app) (app.cxy:7)
    #1  leakCycle (in app) (app.cxy:21)
    #2  _main (in app) (app.cxy:33)
    #3  main (in app) (app.cxy:39)
[LEAK] ptr=0x7bc6c04cb0 size=16 refs=1 @ app.cxy:20  <- cycle of 2 nodes, held by 0x7bc6c04d10
  ...
Total: 2 active allocations (32 bytes leaked)
```

Every line says what leaked, how big it is, how many owners it still has
(`refs`), and **the line that created it**: that is the line of the `Node(...)`
call in your code. For an object that is part of a cycle, the note says so. An
object owned by a named global variable is not reported as a leak, since a global
lives as long as the program does.

If weak references are keeping a destroyed object's memory block, a second section
lists those, see [zombies](#zombies).

### Asking a running program

For a server or any long-running program, the exit report comes too late. When a
traced program starts it prints the path of a socket:

```
[cxy-mem] query socket: /tmp/cxy-mem-57915.sock
```

While it runs, ask it questions from another terminal with `cxy utils mem`:

```
cxy utils mem summary
cxy utils mem --pid 57915 sites 5
```

If exactly one traced program is running the tool finds it on its own, otherwise
name it with `--pid` or `--socket`. Add `nosym` to a command to skip looking up
function names (faster, and the stacks are addresses). `--timeout` sets how long
to wait for an answer, in milliseconds.

The examples below use this program: it keeps 50 nodes in a cache, and leaks one
pair of nodes that own each other.

```cxy
class Node {
    id: i32
    next: Node
    func `init`(id: i32) { this.id = id }
}

class Cache {
    items: Vector[Node]
    func `init`() { items = Vector[Node]() }
    func add(n: Node) { items.push(&&n) }
}

var cache: Cache

func leakCycle() {
    var a = Node(1)           // line 20
    var b = Node(2)           // line 21
    a.next = @copy b
    b.next = @copy a
}

func main() {
    cache = Cache()
    for i in 0..50 {
        cache.add(Node(i))    // line 30
    }
    leakCycle()
    var lonely = Node(100)    // line 33
    sleepAsync(60000)
}
```

### How much is alive, and what

`summary` is the quick look:

```
$ cxy utils mem summary
live: 56 allocations, 1280 bytes
buffers: 1 live, 432 bytes
allocations since start: 56
trace events: 62
mark: 0
```

Objects and **container buffers** (the storage of a `Vector`, a `HashMap`, a
`String`) are counted separately and both are attributed to the line that made or
grew them. `types` groups by class and `sites` by line:

```
$ cxy utils mem types
    allocs      frees       live        bytes  type
        53          0         53          848  Node
         1          0          1          400  coro.CoroutineScheduler
         1          0          1           24  vector.Vector[Node]
         1          0          1            8  Cache

$ cxy utils mem sites 3
       bytes    count  site
         800       50  app.cxy:30
         432        1  app.cxy:13
         400        1  stdlib/coro.cxy:781
```

`types` shows the lifetime of a class (`allocs` and `frees` count over the whole
run, `live` is what is alive now). A class whose `live` keeps growing is your
leak. `sites` tells you where it is made. Here 50 nodes come from line 30 and the
vector buffer (432 bytes) was grown by `push` on line 13.

`list [n]` shows the most recent live objects with the stack that created each:

```
$ cxy utils mem list 2
ptr=0x79ed004d10 size=16 refs=1 @ app.cxy:33
  Allocation callstack:
    #0  Node_op__initCover (in app) (app.cxy:7)
    #1  _main (in app) (app.cxy:33)
    #2  main (in app) (app.cxy:39)
    ...
```

(The first entry, the one before this, is the scheduler the `sleepAsync` call
created.) The stack names the function and the line of every call that led to the
allocation, from `Node(...)` at the top down to `main`.

`sitecounts [n]` is the cumulative version of `sites`: how many allocations each
line has made over the whole run, alive or not. It is the way to find a line that
allocates and frees heavily.

### Is it growing? `mark` and `diff`

To find what a piece of work leaves behind, take a mark before it and compare
after:

```
$ cxy utils mem mark
mark 60
  ... run the request, the action, the test ...
$ cxy utils mem diff
```

`diff` lists the lines of the objects made since the mark that are **still
alive**, `difflist` shows them like `list`. If a request frees everything it
made, `diff` says `no live allocations`.

### Who is holding it? the ownership graph

A leaked object is held by something. These commands follow the owners. They look
at a copy of your program made for the question, so they are exact, and the
program is paused for a moment while it is taken (if a thread does not stop in
time the answer says it is approximate).

`holders` lists what owns an object, and `path` shows the chain from a named
global down to it:

```
$ cxy utils mem path 0x79c6c00bf0
retention path (4 holders deep):
  global cache (0x10423d6b0, 8 bytes)
    -> 0x104ac1810 size=8 refs=1 @ app.cxy:28
      -> 0x104ac1a90 size=24 refs=1 @ app.cxy:12
        -> 0x104ac2490 size=432 buffer @ app.cxy:13
          -> 0x79c6c00bf0 size=16 refs=1 @ app.cxy:30

$ cxy utils mem holders 0x79c6c00bf0
0x79c6c00bf0 size=16 refs=1 @ app.cxy:30
  held by 0x104ac2490 size=432 buffer @ app.cxy:13
```

Read it from the top: the global `cache` owns a `Cache`, which owns a `Vector`,
which owns the buffer, which holds the node. That is why the node is alive. To
free it, remove it from the cache.

Other graph commands:

| Command | Answers |
| --- | --- |
| `edges <ptr>` | what this one object owns |
| `tree <ptr> [depth]` | what it owns, level by level |
| `contains <y> <x>` | is `x` owned, directly or not, by `y` |
| `roots` | the global variables that own something, and what they own |
| `retained [n]` | which roots keep the most memory alive, with the lines that made it |

```
$ cxy utils mem roots
global cache (0x104f8d6b0, 8 bytes)
  cache -> 0x1056d1810 object size=8 @ app.cxy:28
Total: 3 roots, 1 edges

$ cxy utils mem retained 2
  excl.bytes  excl.objs  reach.bytes reach.objs  root
        1264         53         1264         53  global cache (0x104f8d6b0, 8 bytes)
               800 bytes       50 objs  app.cxy:30
               432 bytes        1 objs  app.cxy:13
                24 bytes        1 objs  app.cxy:12
```

`retained` is the answer to "why is the program using so much memory": the cache
alone keeps 1264 bytes alive, nearly all of it nodes from line 30.

### Cycles

Objects that only own each other can never be released. `cycles` finds them:

```
$ cxy utils mem cycles
cycle: 2 nodes, 32 bytes
  0x7481004cb0 size=16 refs=1 @ app.cxy:21
  0x7481004c50 size=16 refs=1 @ app.cxy:20
Total: 1 leaked cycles, 32 bytes
```

`path` on one of them says `no root: every chain of holders ends in a cycle`. The
fix is to make one side of the relation a [weak reference](#cycles-and-weak-references).
Once it is, the cycle is gone from `cycles` and the objects from the exit report.

### Dangling borrows

`dangling` finds `&T` fields that point at an object which no longer exists, even
in a build where nothing has crashed yet. For each one it prints the owner, and
the timeline of the destroyed object: where it was allocated and where it died.

Run on the `View` program from [Borrows can dangle](#borrows-can-dangle), after
`n = Node(2)` (`var n = Node(1)` is line 14, `View(&n)` line 15, `n = Node(2)`
line 16):

```
$ cxy utils mem dangling
dangling: View 0x1056ada30 @ dangling.cxy:15 borrows 0x1056ad810, which is no longer live

--- Lifecycle Timeline for 0x1056ad810 in __cxy_mem_ring ---
  [1] alloc refs=1   @ dangling.cxy:14
    #0  0x104d835dc
    ...
  [2] drop  refs=1   @ dangling.cxy:16
    #0  0x104d8355c
    ...
---------------------------------------------------

Total: 1 dangling borrows
```

The first node was created on line 14 and destroyed on line 16 when `n` was
assigned, while the `View` made on line 15 still points at it. (The frames under
each event are raw addresses, the lines are what you read.)

### Zombies

When weak references outlive their object, the memory block stays until the last
one goes. `zombies` lists them, with how many weak references are left:

```cxy
var n = Node(1)                   // line 9
var w = Weak[Node](n)
var w2 = @copy w
n = Node(2)                       // the first node is destroyed
```

```
$ cxy utils mem zombies
ptr=0x100a01810 size=4 weak=2 @ app.cxy:9
Total: 1 zombies (4 bytes)

$ cxy utils mem summary
live: 2 allocations, 404 bytes
buffers: 0 live, 0 bytes
zombies: 1 destroyed, 4 bytes
...
```

`summary` shows the `zombies:` line only when there is one.

A zombie is not a live object, it does not show in `list`, `sites` or the graph
commands. A few are normal. A zombie that stays forever means a weak reference
that is never released, usually one held by a leaked object, and the exit report
lists those under `Weak references keep destroyed objects alive`.

### All the commands

| Command | Answers |
| --- | --- |
| `summary` | live objects and buffers, bytes, totals |
| `types` | allocations, frees and live count per class |
| `sites [n]` | live objects per line of code |
| `sitecounts [n]` | all allocations ever per line of code |
| `list [n]` | the latest live objects with their stacks |
| `leaks` | the full report, like the one printed at exit |
| `mark`, `diff [n]`, `difflist [n]` | what was allocated since the mark and is still alive |
| `holders <ptr>`, `path <ptr>`, `contains <y> <x>` | who owns an object, and how it is reached |
| `edges <ptr>`, `tree <ptr> [depth]` | what an object owns |
| `roots`, `retained [n]`, `cycles [n]` | global owners, what they keep alive, leaked cycles |
| `dangling`, `zombies [n]` | borrows of destroyed objects, destroyed objects kept by weak references |
| `help` | this list |

Addresses (`<ptr>`) come from the other commands: copy them from a `list`, `cycles`
or `holders` line.

### Good to know

- Only objects made after the program started its `main` are traced. Anything
  created before it (global state made by the runtime) is never reported.
- `CXY_MEM_SOCKET=0` in the program's environment turns the socket off (the exit
  report still prints), `CXY_MEM_SOCKET=/some/path` chooses the path.
- Graph commands give up after two minutes by default, `CXY_MEM_SCAN_SECONDS` in
  the program's environment changes that.
- The trace and the checks of a debug build work together: build with
  `--memory-trace` while you look for a leak, and read the panic messages and the
  `dangling` command when something crashes.

## Cheat sheet

| I want to... | Write |
| --- | --- |
| share an object | `var b = a` |
| get a new object with the same contents | `a.clone()` |
| give an object away | `f(&&a)`, `return a` |
| let a function read an object | `func f(n: &Node)` |
| keep an object a `&T` points to | `@copy r` |
| point at an object without keeping it alive | `@weak Node` / `Weak[Node](n)` |
| use a weak reference | `if (w.upgrade()) { ... }` |
| forbid copies of a type | `@nocopy` |
| have the compiler check my ownership | `--warnings=Ownership` |
| find a leak | `--memory-trace`, then `cxy utils mem` |
