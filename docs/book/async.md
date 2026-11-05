# Asynchronous Programming with Coroutines

Cxy runs concurrent code on **coroutines**: lightweight, cooperatively scheduled
routines with their own stack. A coroutine runs until it waits for something (a
timer, a socket, a channel, another coroutine) and then lets another one run.
There is no preemption, so code between two waits is never interrupted.

Everything lives in `stdlib/coro.cxy`. A program gets a scheduler per thread on
first use; the code that runs `main` is the *main coroutine*.

```cxy
import "stdlib/coro.cxy"
```

## Starting a coroutine: `async`

`async` runs a statement on a new coroutine and does not wait for it:

```cxy
func main() {
    async println("hello from a coroutine")
    println("hello from main")
}
```

The new coroutine runs first, up to its first wait, then control returns to the
caller. `async "name" stmt` gives the coroutine a name that shows up in traces.

`async` is fire-and-forget: you cannot wait for the coroutine, cancel it or read a
result. When you need that, use a task.

## Waiting

A coroutine gives up the processor by calling one of the blocking operations.
They are all non-blocking for the rest of the program:

| Call | Waits for |
| --- | --- |
| `sleepAsync(ms)` | a timer |
| `coro.yld()` | nothing, other ready coroutines run first |
| `fdWaitRead(fd)`, `fdWaitWrite(fd)` | a file descriptor |
| `timeout(ms)` | a timer, returns `true` if it fired |
| `Channel.send` / `Channel.receive` | the other side of a channel |
| `CoWaitGroup.wait()` | a counter reaching zero |

```cxy
async {
    sleepAsync(100)
    println("100ms later")
}
```

## Channels

`Channel[T]` passes values between coroutines. `receive` returns an optional:

```cxy
var ch = Channel[i32](4)          // buffer of 4

async {
    for (const i: 0..3) {
        ch.send(i)
    }
}

while true {
    var v = ch.receive(1000)      // wait at most 1000ms
    if v == null
        break
    println(*v)
}
```

`send(x, ms)` and `receive(ms)` take an optional timeout. `receive` returns
`null` on timeout.

## Waiting for several coroutines

`CoWaitGroup` counts running work:

```cxy
var wg = CoWaitGroup("workers")
for (const i: 0..5) {
    wg.add()
    async {
        sleepAsync(10)
        wg.done()
    }
}
wg.wait()
```

## Tasks: handles you can join and cancel

A **task** is a coroutine with a handle. Mark the function with `@task` and start
it with `@async`. The expression has type `Task[T]`, where `T` is the function's
return type:

```cxy
@task
func double(x: i32): i32 {
    sleepAsync(10)
    return x * 2
}

func main() {
    var t = @async double(21)
    var v = t.join() catch {
        println("task failed")
        return
    }
    println(*v)                  // 42 if join returned a value
}
```

Rules:

- `@task` functions need an explicit return type (`: void` if there is none) and
  cannot be generic, variadic, operator overloads or local functions.
- `@async` only accepts a call to a `@task` function (free function, method or
  static method) and its result must be used.
- `@async f(args)` is only allowed where the call is the whole operand:
  `var t = @async f()`, `t = @async f()`, `return @async f()`.
- Dropping the handle detaches the task, it keeps running.

### Errors

A `@task` function can be declared `!T` and can `raise`. The error is stored in the
task and raised again by `join()`:

```cxy
@task
func mayFail(): !i32 {
    raise Exception("boom")
}

var t = @async mayFail()
t.join() catch as ex {
    println("failed: ", ex.what())
}
```

### The `Task` API

| Method | Meaning |
| --- | --- |
| `join(): !T` | wait until the task finishes; returns its value or raises its error |
| `cancel()` | ask the task to stop (see below), does not wait |
| `done(): bool` | the task has finished |
| `cancelled(): bool` | `cancel()` was called |
| `wake(yieldNow = false)` | resume a task blocked in `park()` early; with `yieldNow` the caller yields so the task runs now |

Inside a task, `currentTask!()` gives the task's own handle:

| Method | Meaning |
| --- | --- |
| `park(ms = 0): bool` | block until woken, `ms` elapsed (`ms > 0`) or cancelled; `false` only when cancelled |
| `yld(): bool` | yield to other coroutines; `false` once cancelled |

`park` is for tasks that sleep until there is work. A `wake()` that arrives before
the task parks is remembered, so the next `park()` returns immediately:

```cxy
@task
func worker(queue: Queue): void {
    var task = currentTask!()
    while task.park() {          // false when cancelled
        queue.drain()
    }
}

var t = @async worker(queue)
queue.push(job)
t.wake()
...
t.cancel()
t.join() catch discard
```

## Cancellation

Cancellation is **cooperative**. A coroutine's stack holds live objects, so it is
never killed; `cancel()` sets a flag and interrupts the wait the task is in, then
the task's own code returns and cleans up. Always follow `cancel()` with `join()`
when the task uses memory you are about to free.

A cancel interrupts these waits immediately: `sleepAsync`, `timeout`,
`fdWaitRead/Write`, `Channel.send/receive` and `Task.park`. The wait returns early
and the code after it should check for cancellation:

```cxy
@task
func reader(fd: i32): void {
    var task = currentTask!()
    while !task.cancelled() {
        fdWaitRead(fd)
        ...
    }
}
```

Waits on `CoWaitGroup`, `Semaphore`, `Mutex`, `ConditionVariable`, `WaitGroup`,
`SharedChannel` and `join()` itself are not interrupted: the flag is set and the
task notices at its next interruptible wait or when it checks `cancelled()`.

A long loop that never waits should call `task.yld()` and stop when it returns
`false`.

## Good practice

- A task that borrows an object (for example a member function using `this`) must
  be cancelled and joined before the object is destroyed. A common pattern is
  to store the `Task` in the object and `cancel(); join()` in `deinit`.
- Keep the arguments to `@async f(...)` simple. They are evaluated in the spawning
  function before the task starts, but temporaries that function cleans up after
  the call run later on the task's stack.
- Do not block the thread (`sleep`, blocking IO) inside a coroutine; use the async
  versions so other coroutines keep running.

## Configuration and debugging

- Each coroutine has a stack of 256 KiB by default. Define `CORO_STACK_SIZE` to
  change it.
- Coroutine tracing needs a debug build and a trace function. For example,
  `--debug --define TRACE_CORO=__trace_coro_stdout` prints spawn, suspend, switch
  and exit events for every coroutine.
- Threads: with threads enabled, every thread has its own scheduler and
  coroutines stay on the thread that started them. `Task.cancel`, `wake` and
  `join` can be called from another thread, but the task's fields are not yet
  synchronised, so do that only for simple handoffs.
