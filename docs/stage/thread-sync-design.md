# Thread-Aware Synchronization Primitives for Cxy

## Problem Statement

Currently, Cxy has two separate concurrency models:

1. **Coroutines** (`coro.cxy`): Cooperative multitasking within a thread
   - Each thread owns a `CoroutineScheduler`
   - Coroutines yield cooperatively via `suspend()`/`resume()`
   - `Mutex` works only within a single thread (not thread-safe)

2. **Threads** (`thread.cxy`): OS-level parallelism
   - True parallel execution across CPU cores
   - No synchronization primitives for cross-thread coroutine coordination

**The Gap**: When coroutine A (thread 1) needs to wait for a resource held by coroutine B (thread 2), we have no proper synchronization mechanism that:
- Doesn't block the entire OS thread (would freeze all coroutines in that thread)
- Allows other coroutines to run while waiting
- Properly wakes up waiting coroutines when resources become available

## Design Goals

1. **Non-blocking for coroutines**: Waiting on a lock should yield to other coroutines, not block the thread
2. **Thread-safe**: Primitives must work correctly when accessed from multiple OS threads
3. **Fair**: Avoid starvation, provide reasonable ordering guarantees
4. **Efficient**: Minimize overhead for both contended and uncontended cases
5. **Composable**: Work seamlessly with existing coroutine and thread primitives

## Design Summary

This design achieves thread-safe coroutine synchronization with **zero allocations** through three key insights:

### 1. Reuse Existing Coroutine Structure
Every `Coroutine` already has:
- `link: ^This` — Used for intrusive queue linking
- `scheduler: ^void` — Knows which scheduler owns it
- `result: i32` — For passing resume results

No wrapper structures needed! The coroutine itself serves as the queue node.

### 2. Single Queue Membership Invariant
A coroutine is in exactly one state at any time:
- **Running**: `link = null` (not in any queue)
- **Ready**: In `scheduler.ready` queue
- **Waiting**: In `mutex.waiters` or `condition.waiters` queue
- **Pending**: In `scheduler.pending` queue (cross-thread resume)

Since states don't overlap, we can safely reuse `Coroutine.link` across all queue contexts.

### 3. Leverage Existing List[Coroutine] Type
The codebase already has `List[T]` — an intrusive linked list with:
- FIFO semantics (push to tail, pop from head)
- O(1) operations
- Zero allocation (uses `T.link` field)

We simply use `List[Coroutine]` for all queues:
- `ThreadMutex.waiters: List[Coroutine]`
- `ThreadCondition.waiters: List[Coroutine]`
- `CoroutineScheduler.pending: List[Coroutine]`

### Eliminated Complexity
Compared to the original design draft:
- ❌ No `WaitQueueEntry` wrapper struct
- ❌ No `PendingResumption` wrapper struct
- ❌ No `WaitQueue` abstraction
- ❌ No heap allocations on wait/resume operations
- ✅ Just `List[Coroutine]` everywhere

### Performance Impact
- **Fast path**: Uncontended lock remains ~10-20 cycles (no change)
- **Slow path**: No allocation overhead (~500-1000 cycle savings)
- **Cross-thread**: No allocation overhead (~500-1000 cycle savings)
- **Memory**: Zero heap pressure from synchronization primitives

This simplification makes the design easier to implement, debug, and reason about while maintaining thread-safety guarantees through OS mutex protection of queue operations.

## Architecture Overview

### Components

```
┌─────────────────────────────────────────────────────────────┐
│ Thread 1                                                    │
│  ┌────────────────────────────────────────────────────┐     │
│  │ CoroutineScheduler                                 │     │
│  │  - running: ^Coroutine                             │     │
│  │  - ready: List[Coroutine]                          │     │
│  │  - pending: List[Coroutine]         ◄──────┐       │     │
│  │  - pendingMutex: mtx_t                     │       │     │
│  └────────────────────────────────────────────┼───────┘     │
│       ▲                                       │             │
│       │ resume from other thread              │             │
└───────┼───────────────────────────────────────┼─────────────┘
        │                                       │
        │                                       │ thread-safe
        │                                       │ enqueue
        │                                       │
┌───────┼───────────────────────────────────────┼─────────────┐
│ Thread 2                                      │             │
│  ┌────────────────────────────────────────────┼───────┐     │
│  │ CoroutineScheduler                         │       │     │
│  │  - running: ^Coroutine                     │       │     │
│  │  - ready: List[Coroutine]                  │       │     │
│  │  - pending: List[Coroutine]  ──────────────┘       │     │
│  │  - pendingMutex: mtx_t                             │     │
│  └────────────────────────────────────────────────────┘     │
│                                                             │
│  ThreadMutex (shared)                                       │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ - osMutex: mtx_t (protects waiters list)            │    │
│  │ - locked: bool                                      │    │
│  │ - holder: ^Coroutine                                │    │
│  │ - waiters: List[Coroutine]                          │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### Key Data Structures

**Simplified Design - Zero Allocations**: We use existing `List[Coroutine]` intrusive linked lists for all queues. No wrapper structures needed!

#### Coroutine Structure (Existing)
```cxy
struct Coroutine {
    link: ^This               // Used for intrusive linking in queues
    scheduler: ^void          // Knows its scheduler
    result: i32               // For passing results
    ready: bool
    // ... other fields
}
```

**Key Insight**: `Coroutine.link` is reused across different queue contexts:
- When in `ready` queue: link points to next ready coroutine
- When in `waiters` queue: link points to next waiting coroutine  
- When in `pending` queue: link points to next pending coroutine
- When running: link is null

A coroutine is only in **one** queue at a time, so there are no conflicts.

#### List[Coroutine] (Existing)
```cxy
struct List[T] {
    _head: ^T
    _tail: ^T
    count: u64
    
    func push(item: ^T)  // FIFO enqueue to tail
    func pop(): ^T       // Dequeue from head
}
```

This provides:
- ✅ FIFO ordering (fair scheduling)
- ✅ Zero allocation (intrusive via `link` field)
- ✅ O(1) enqueue/dequeue
- ✅ Already tested and battle-hardened

## Detailed Design: ThreadMutex

### Structure
```cxy
pub struct ThreadMutex {
    - osMutex: tinyThread.mtx_t      // Protects waiters list
    - locked: bool                    // Lock state
    - holder: ^Coroutine              // Current lock holder
    - waiters: List[Coroutine]        // FIFO queue of waiting coroutines
}
```

### API

```cxy
func `init`()                        // Create mutex
func lock()                          // Acquire lock (may suspend)
func tryLock(): bool                 // Non-blocking acquire
func unlock()                        // Release lock
func `deinit`()                      // Cleanup
```

### lock() Algorithm

```
1. Fast path (uncontended):
   a. Check if locked == false
   b. If false, set locked = true
   c. Set holder = running()
   d. Return (no OS mutex needed)

2. Slow path (contended):
   a. mtx_lock(osMutex)
   b. Add current coroutine to waiters list:
      waiters.push(running())  // Zero allocation!
   c. mtx_unlock(osMutex)
   d. suspend()  // Yields to other coroutines
   e. When resumed, lock is acquired

Note: No allocations! Coroutine.link is reused for waiters queue.
```

### unlock() Algorithm

```
1. Verify caller is holder
   - If not, error/panic

2. mtx_lock(osMutex)

3. Dequeue next waiter from waiters list
   a. If queue is empty:
      - Set locked = false, holder = null
      - mtx_unlock(osMutex)
      - Return
   
   b. If waiter exists:
      - Update holder = waiter
      - Get waiter's scheduler: waiter.scheduler !: ^CoroutineScheduler
      
      - If waiter is from current thread:
         * Add to current scheduler's ready list
         * ready.push(waiter)  // Reuses Coroutine.link
      
      - If waiter is from different thread:
         * mtx_lock(waiter.scheduler.pendingMutex)
         * Add to target scheduler's pending list
         * pending.push(waiter)  // Reuses Coroutine.link
         * mtx_unlock(waiter.scheduler.pendingMutex)

4. mtx_unlock(osMutex)

Note: Zero allocations! Coroutine.link moves from waiters → ready/pending.
```

### tryLock() Algorithm

```
1. Check if locked == false
2. If false, atomically set locked = true
3. Set holder = running(), holderScheduler = current scheduler
4. Return true

5. If locked == true, return false immediately
   (No waiting, no suspension)
```

## Detailed Design: ThreadCondition

Condition variables for thread-safe signaling.

### Structure
```cxy
pub struct ThreadCondition {
    - osMutex: tinyThread.mtx_t       // Protects waiters list
    - waiters: List[Coroutine]         // Waiting coroutines
}
```

### API

```cxy
func `init`()
func wait(mutex: &ThreadMutex)       // Wait for signal (releases mutex, may suspend)
func signal()                         // Wake one waiter
func broadcast()                      // Wake all waiters
func `deinit`()
```

### wait() Algorithm

```
1. Verify mutex is held by current coroutine
   - If not, error/panic

2. mtx_lock(osMutex)
3. Add current coroutine to waiters:
   waiters.push(running())  // Zero allocation!
4. mtx_unlock(osMutex)

5. mutex.unlock()  // Release the associated mutex

6. suspend()  // Wait for signal

7. mutex.lock()  // Re-acquire mutex before returning
```

### signal() Algorithm

```
1. mtx_lock(osMutex)

2. Dequeue one waiter from waiters list
   - If queue empty:
      * mtx_unlock(osMutex)
      * Return

3. Resume the waiter:
   - Get scheduler: waiter.scheduler !: ^CoroutineScheduler
   - Same thread: scheduler.ready.push(waiter)
   - Different thread: 
      * Lock scheduler.pendingMutex
      * scheduler.pending.push(waiter)
      * Unlock scheduler.pendingMutex

4. mtx_unlock(osMutex)
```

### broadcast() Algorithm

```
1. mtx_lock(osMutex)

2. While waiters list not empty:
   a. Dequeue waiter: waiters.pop()
   b. Resume waiter (same/different thread logic as signal)

3. mtx_unlock(osMutex)
```

## CoroutineScheduler Modifications

### New Fields
```cxy
class CoroutineScheduler {
    // ... existing fields ...
    
    - pending = List[Coroutine]{}   // Cross-thread resumptions
    - pendingMutex: tinyThread.mtx_t
}
```

### Modified suspend() Function

```cxy
func suspend(): i32 {
    // Process pending resumptions from other threads
    processPending()
    
    if (counter >= 103) {
        eventLoopWait(0)
        counter = 0
    }
    
    if (running != null) {
        if (__cxy_coro_setjmp!(ptrof this.running.ctx)) {
            return running.result
        }
    }
    
    while {
        // Process pending again before checking ready queue
        processPending()
        
        var coro = ready.pop()
        if (coro != null) {
            counter++
            assert!(coro.ready)
            coro.ready = false
            running = coro
            __cxy_coro_longjmp!(ptrof coro.ctx)
        }
        
        eventLoopWait()
        counter = 0
    }
    return 0
}
```

### New processPending() Function

```cxy
- func processPending() {
    // Fast path: check without locking
    if (pending.empty())
        return
    
    // Lock and drain the queue
    tinyThread.mtx_lock(ptrof pendingMutex)
    
    while {
        var coro = pending.pop()
        if (coro == null)
            break
        
        // Move to ready queue (Coroutine.link reused!)
        coro.ready = true
        ready.push(coro)
    }
    
    tinyThread.mtx_unlock(ptrof pendingMutex)
}
```

### Initialization

```cxy
func `init`() {
    running = ptrof main
    eventLoop = ae.aeCreateEventLoop(this !: ^void, 1024)
    
    // Initialize pending resumptions mutex
    tinyThread.mtx_init(ptrof pendingMutex, mtx_plain!)
}
```

### Cleanup

```cxy
func `deinit`() {
    tinyThread.mtx_destroy(ptrof pendingMutex)
}
```

## Memory Management Strategy

### Allocation Strategy

**Zero-allocation design** using intrusive linked lists:

1. **No WaitQueueEntry needed**: 
   - Coroutine structs are already allocated (stack-based)
   - Use `Coroutine.link` field for queue linking
   - Zero allocation on wait/resume operations

2. **No PendingResumption needed**:
   - Coroutines move directly from waiters → pending → ready
   - Same `Coroutine.link` field reused across queue transitions
   - Zero allocation for cross-thread signaling

3. **List[Coroutine] queues**:
   - Embedded in ThreadMutex/ThreadCondition/CoroutineScheduler
   - Intrusive linked list (no wrapper allocations)
   - All operations are O(1) pointer manipulations

### Ownership Rules

```
Coroutine.link lifecycle (zero allocation):
  [Coroutine suspends, enters waiters queue]
       ↓
  [Linked via Coroutine.link in waiters]
       ↓
  [Dequeued by unlocker/signaler]
       ↓
  ┌──────────────────┐
  │ Same thread?     │
  └──────────────────┘
    Yes ↓         ↓ No
  [Push to     [Push to target
   ready list]  scheduler.pending]
       ↓              ↓
  [Link reused] [Target thread's
                 processPending() 
                 moves to ready]
                      ↓
                 [Link reused]
```

**Key insight**: Coroutine struct itself is stable (stack-allocated), 
only the `link` field changes as it moves between queues.

### Memory Safety Considerations

1. **No double-free**: Coroutines never freed by sync primitives
2. **No use-after-free**: Coroutine structs remain valid during suspension
3. **No leaks**: No allocations to leak!
4. **ABA problem**: Not applicable (stable coroutine pointers)
5. **Queue safety**: OS mutexes protect all queue operations

## Fairness and Scheduling Strategy

### FIFO Ordering (Fair Scheduling)

**Rationale**: FIFO provides:
- Predictable behavior
- Starvation prevention
- Simple implementation
- Works well with cooperative scheduling

**Implementation**:
- List[Coroutine] maintains head/tail pointers
- push() adds to tail
- pop() removes from head
- O(1) operations
- Zero allocation overhead

### Priority Extension (Future)

If priority scheduling needed later, could extend Coroutine:

```cxy
struct Coroutine {
    link: ^This
    scheduler: ^void
    priority: i32 = 0  // Future: priority level
    // ... other fields
}
```

Could implement:
- Priority queue instead of FIFO (would need custom List implementation)
- Priority inheritance to prevent priority inversion
- Per-priority lists (multiple List[Coroutine] per mutex/condition)
- Sort on insertion or use multiple queues by priority level

### Fairness Guarantees

1. **ThreadMutex**: FIFO acquisition order via List[Coroutine]
   - First to wait is first to acquire (tail insertion, head removal)
   - No thread can monopolize lock
   - All coroutines treated equally regardless of origin thread

2. **ThreadCondition**: 
   - signal(): Wakes oldest waiter (FIFO via pop())
   - broadcast(): Wakes all waiters in order, they compete fairly for mutex
   - Uses same List[Coroutine] FIFO semantics

3. **Cross-thread fairness**: 
   - No preference for same-thread vs cross-thread waiters
   - All waiters treated equally in waiters queue
   - Pending queue processes all cross-thread resumes in FIFO order

## Performance Considerations

### Fast Paths

1. **Uncontended lock**:
   - No OS mutex acquisition
   - Simple check + set (locked = true)
   - Zero allocation
   - ~10-20 CPU cycles

2. **Same-thread unlock with waiter**:
   - Single OS mutex lock/unlock
   - Direct ready queue insertion via List[Coroutine].push()
   - Zero allocation (Coroutine.link reused)
   - ~100-200 CPU cycles

### Slow Paths

1. **Contended lock**:
   - OS mutex lock/unlock
   - Wait queue insertion via List[Coroutine].push()
   - Coroutine suspend/resume context switch
   - Zero allocation (intrusive linking)
   - ~1000-5000 CPU cycles

2. **Cross-thread unlock**:
   - Two OS mutex operations (waiters + pendingMutex)
   - List[Coroutine] operations (pop from waiters, push to pending)
   - Zero allocation (Coroutine.link reused)
   - Wakeup latency depends on target scheduler polling
   - ~2000-10000 CPU cycles (no allocation overhead!)

### Optimization Opportunities

1. **Event-based wakeup**: Integrate with event loop for immediate cross-thread signals
   - Use eventfd (Linux) or kqueue (BSD) to wake target thread immediately
   - Currently relies on polling in suspend() loop
2. **Batch processing**: Already implemented! processPending() drains entire queue at once
3. **Adaptive spinning**: Brief spin before suspending on contended locks
   - Try a few iterations before calling suspend()
   - Useful for very short critical sections
4. **Lock elision**: Compiler/hardware transactional memory for uncontended fast path

## Implementation Plan

### Phase 1: Core Infrastructure (coro.cxy)
- [ ] Add pending: List[Coroutine] + pendingMutex to CoroutineScheduler
- [ ] Implement processPending() function
- [ ] Integrate processPending() into suspend() loop
- [ ] Add initialization/cleanup for pendingMutex
- [ ] Unit tests for cross-thread coroutine resumption

### Phase 2: ThreadMutex (thread.cxy)
- [ ] Implement ThreadMutex structure (osMutex, locked, holder, waiters: List[Coroutine])
- [ ] Implement lock() algorithm (fast path + wait queue)
- [ ] Implement unlock() algorithm (same-thread vs cross-thread resume)
- [ ] Implement tryLock() (non-blocking acquire)
- [ ] Unit tests for single-thread scenario
- [ ] Unit tests for cross-thread scenario
- [ ] Stress tests (high contention, fairness validation)

### Phase 3: ThreadCondition (thread.cxy)
- [ ] Implement ThreadCondition structure (osMutex, waiters: List[Coroutine])
- [ ] Implement wait() algorithm (unlock, suspend, re-lock)
- [ ] Implement signal() algorithm (wake one waiter)
- [ ] Implement broadcast() algorithm (wake all waiters)
- [ ] Unit tests (single/multiple waiters)
- [ ] Integration tests with ThreadMutex (producer-consumer, etc.)

### Phase 4: Documentation & Examples
- [ ] API documentation
- [ ] Usage examples
- [ ] Performance benchmarks
- [ ] Migration guide from Mutex to ThreadMutex

## API Examples

### Example 1: Simple Mutex

```cxy
import { ThreadMutex } from "stdlib/thread.cxy"

var counter = 0
var mutex = ThreadMutex()

func incrementCounter() {
    mutex.lock()
    counter++
    mutex.unlock()
}

func main() {
    var t1 = launch { 
        async { for i in 0..1000 { incrementCounter() } }
    }
    var t2 = launch { 
        async { for i in 0..1000 { incrementCounter() } }
    }
    
    t1.join()
    t2.join()
    
    println("Counter: ", counter)  // Always 2000
}
```

### Example 2: Condition Variable

```cxy
import { ThreadMutex, ThreadCondition } from "stdlib/thread.cxy"

var queue = Vector[i32]()
var mutex = ThreadMutex()
var cond = ThreadCondition()

func producer() {
    for i in 0..10 {
        mutex.lock()
        queue.push(i)
        cond.signal()  // Wake one consumer
        mutex.unlock()
    }
}

func consumer() {
    while true {
        mutex.lock()
        
        while queue.isEmpty() {
            cond.wait(&mutex)  // Wait for items
        }
        
        var item = queue.pop()
        mutex.unlock()
        
        println("Consumed: ", item)
    }
}

func main() {
    var t1 = launch { async { producer() } }
    var t2 = launch { async { consumer() } }
    var t3 = launch { async { consumer() } }
    
    t1.join()
}
```

### Example 3: Reader-Writer Pattern

```cxy
import { ThreadMutex, ThreadCondition } from "stdlib/thread.cxy"

class RWLock {
    - mutex = ThreadMutex()
    - canRead = ThreadCondition()
    - canWrite = ThreadCondition()
    - readers: i32 = 0
    - writers: i32 = 0
    - writeRequests: i32 = 0
    
    func readLock() {
        mutex.lock()
        while writers > 0 || writeRequests > 0 {
            canRead.wait(&mutex)
        }
        readers++
        mutex.unlock()
    }
    
    func readUnlock() {
        mutex.lock()
        readers--
        if readers == 0 {
            canWrite.signal()
        }
        mutex.unlock()
    }
    
    func writeLock() {
        mutex.lock()
        writeRequests++
        while readers > 0 || writers > 0 {
            canWrite.wait(&mutex)
        }
        writeRequests--
        writers++
        mutex.unlock()
    }
    
    func writeUnlock() {
        mutex.lock()
        writers--
        if writeRequests > 0 {
            canWrite.signal()
        } else {
            canRead.broadcast()
        }
        mutex.unlock()
    }
}
```

## Open Questions

1. **Deadlock detection**: Should we implement deadlock detection/prevention?
   - Could track lock ordering
   - Could implement timeouts
   - Could provide try_lock_for(duration) API

2. **Recursive mutexes**: Should ThreadMutex support recursion?
   - Could add recursion counter
   - Would need to track depth per holder
   - Increases complexity

3. **Integration with async/await**: Future integration points?
   - Could ThreadMutex be awaitable?
   - Should unlock trigger await continuations?

4. **Scoped locks**: RAII-style lock guards?
   ```cxy
   {
       var guard = LockGuard(mutex)
       // ... critical section ...
   }  // auto-unlock on scope exit via deinit
   ```

5. **Lock statistics**: Should we track contention metrics?
   - Lock acquisition counts
   - Wait times
   - Contention rates
   - Helpful for optimization

## Testing Strategy

### Unit Tests
- Single-threaded lock/unlock cycles (verify FIFO ordering)
- tryLock behavior (non-blocking semantics)
- Multiple locks (different mutexes, verify independence)
- Condition variable signal/broadcast (FIFO wake order)
- Edge cases (unlock without lock, double unlock, etc.)
- Coroutine.link integrity (verify not in multiple queues simultaneously)
- Zero-allocation validation (no heap allocations during wait/resume)

### Integration Tests
- Cross-thread lock contention (verify pending queue mechanics)
- Multiple threads, multiple coroutines per thread
- Condition variable with multiple waiters (signal vs broadcast)
- Producer-consumer patterns (verify no lost wakeups)
- Reader-writer patterns (fairness validation)
- Rapid lock/unlock cycles (verify Coroutine.link state transitions)
- Cross-thread signal latency measurement

### Stress Tests
- High contention scenarios (many coroutines per mutex)
- Long-running tests (verify queue state consistency, no corruption)
- Random lock/unlock patterns (chaos testing)
- Fairness validation (histogram of acquisition order, verify FIFO)
- Queue integrity under stress (verify List[Coroutine] operations)
- Cross-thread storm (rapid cross-thread signaling)
- Memory stability (no leaks, even though zero-allocation by design)

### Performance Tests
- Uncontended lock latency (fast path validation)
- Contended lock throughput (same-thread waiters)
- Cross-thread wake-up latency (pending queue overhead)
- Comparison with OS-only mutexes (pthread_mutex)
- Scalability (1-16 threads, multiple coroutines per thread)
- Zero-allocation overhead (compare to allocation-based designs)
- List[Coroutine] operation costs (push/pop microbenchmarks)

### Memory Safety Tests
- Verify Coroutine.link is null when running
- Verify coroutine only in one queue at a time
- Race condition testing (concurrent lock/unlock/signal)
- Use-after-resume safety (verify coroutine stable during queue transitions)
- Queue consistency checks (head/tail pointer validity)

## Future Enhancements

1. **Read-Write Locks**: Native ThreadRWLock implementation
2. **Semaphores**: Counting semaphores for resource pools
3. **Barriers**: Synchronize multiple threads/coroutines at barrier points
4. **Lock-free structures**: Lock-free queues, stacks for high performance
5. **Async channels**: Extend Channel to work across threads
6. **Work stealing**: Cross-thread coroutine work-stealing scheduler
