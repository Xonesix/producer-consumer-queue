# Concurrent Queues in C++

Four ways for many threads to share one queue safely, starting with a simple mutex and ending with a
lock-free queue that uses hazard pointers. Each one is benchmarked against
[moodycamel::ConcurrentQueue](https://github.com/cameron314/concurrentqueue), a widely used
production library.

This is a self-teaching project based on *C++ Concurrency in Action* (Anthony Williams), chapters 4, 6
and 7. I wrote the queue implementations by hand. Two files are not mine: `concurrentqueue.h` is
moodycamel's library, and `bench.py`, the benchmark runner, was written with AI assistance.

---

## Results

Every program pushes and pops **1,000,000 items** and checks that nothing was lost or duplicated.
`bench.py` ran each program **2,000 times** and recorded how long each full run took.

| Queue | Threads (producers/consumers) | p50 | p99 | p99 vs p50 | Throughput at p50 |
|---|---|---:|---:|---:|---:|
| `lockFree.cpp`: lock-free, single producer/consumer | 1 / 1 | 92.8 ms | 255.7 ms | +175% | 10.8 M ops/s |
| `boundedQueue.cpp`: mutex + 2 condition variables, capacity 10k | 10 / 10 | 239.6 ms | 296.5 ms | +24% | 4.2 M ops/s |
| `producer_consumer_MutexQ.cpp`: mutex + condition variable | 10 / 10 | 261.1 ms | 325.3 ms | +25% | 3.8 M ops/s |
| `MPMC_test.cpp`: lock-free Michael–Scott queue + hazard pointers | 10 / 10 | 382.0 ms | 422.6 ms | **+11%** | 2.6 M ops/s |
| moodycamel (baseline) | 1 / 1 | 27.8 ms | 36.4 ms | +31% | 36.0 M ops/s |
| moodycamel (baseline) | 10 / 10 | 56.0 ms | 69.1 ms | +23% | 17.8 M ops/s |

0 failures across all runs. Full output is in [`benchmark_results.txt`](benchmark_results.txt).
Measured on Windows 11 with clang 22, `-O2`.

**What the numbers say**

- **The lock-free queue is the most consistent, but not the fastest.** Its slowest runs are only 11%
  slower than a typical run, compared with about 25% for the mutex queues. Its typical run is still
  slower than theirs.
- **Why it's slower:** every push allocates a node on the heap, and all 20 threads compete for the
  same `head` and `tail`. A failed compare-and-swap (CAS) means retrying, and those retries pile up
  under contention.
- **moodycamel is about 7× faster** because it avoids both problems. Each producer gets its own
  sub-queue, and items are stored in pre-allocated blocks instead of one node per item.
- **The single-producer lock-free queue has a long tail** (p99 is 2.75× p50). The consumer busy-polls
  with `yield()` when the queue is empty, and every item goes through a `shared_ptr` allocation.

---

## The queues

### 1. `producer_consumer_MutexQ.cpp`: mutex + condition variable

The textbook version: a `std::deque` protected by one `std::mutex`. Consumers sleep on a
`condition_variable` instead of spinning when the queue is empty. Each pop records a timestamp, so
the program shows when each producer's last item came out, without needing a second lock.

*What I learned:* why `wait()` needs `unique_lock` and a predicate (spurious wakeups), why to notify
after unlocking, and that one mutex makes every operation take turns.

### 2. `boundedQueue.cpp`: adding a capacity limit

The same design with a maximum size (10,000) and a second condition variable. Consumers wait when
the queue is empty, producers wait when it's full, and each side wakes the other.

*What I learned:* why one condition variable isn't enough once both sides can wait. A full queue
pushing back on producers (backpressure) mirrors an empty queue making consumers wait.

### 3. `lockFree.cpp`: lock-free, one producer and one consumer

A linked list with a dummy node and atomic `head`/`tail`, and no mutex. It's only correct when one
thread pushes and one thread pops: the producer is the only thread that writes `tail` and the
consumer is the only one that writes `head`. That's what makes `delete` safe here.

*What I learned:* the first version tried to support many threads and was broken in two ways. A
push could be interrupted halfway through, and there was no safe moment to free a node because
another thread might still be reading it. Freeing memory safely is the hard part, which leads to
the next file.

### 4. `MPMC_test.cpp`: lock-free, many producers and consumers, with hazard pointers

The Michael & Scott (1996) queue, with hazard pointers for memory safety.

- **Push and pop are each a single CAS.** Push links a new node after the last one. Pop swings
  `head` forward one node.
- **Threads help each other.** If `tail` has fallen behind, any thread that notices moves it forward
  instead of waiting for the thread that fell behind.
- **Hazard pointers:** before reading a node, a thread publishes "I'm using this node" in its own
  slot, then re-checks that the node is still in the queue. A removed node goes on the thread's
  retired list, and it's only freed once no thread's slot points to it. This prevents
  use-after-free and the ABA problem without locks or garbage collection.
- Each thread's slot sits on its own 64-byte cache line (`alignas(64)`), so threads don't slow each
  other down by writing to the same cache line (false sharing).
- Correctness check: each consumer verifies it sees every producer's items in order, the sum of all
  1M values matches the expected total, and the queue is empty at the end.

Usage: `MPMC_test.exe [producers consumers]` (default `10 10`). Up to 64 threads in total.

### Baseline: `MPMC_NotMine.cpp`

The same 1M-item test using moodycamel's `ConcurrentQueue`.
`MPMC_NotMine.exe spsc` runs it with 1 producer and 1 consumer, `MPMC_NotMine.exe mpmc` runs it with
10 producers and 10 consumers, and no argument runs both.

---

## Building and running

There's no build system; each file has its own `main()`. Everything compiles as C++17 or C++20.

```sh
clang++ -std=c++17 -O2 MPMC_test.cpp -o MPMC_test.exe
./MPMC_test.exe 10 10
```

On Linux or macOS, add `-pthread`. `g++` works the same way.

**Benchmark everything** (needs Python 3 and `clang++` on your PATH):

```sh
python bench.py          # 2,000 runs per queue (about 40 minutes)
python bench.py 100      # quick run
```

It compiles each file into `bench_bin/`, runs it the given number of times, and writes p50, p99,
mean, min and max to `benchmark_results.txt`.

---

## Known limitations

- `lockFree.cpp` is only safe with one producer and one consumer, and it doesn't exit with a
  pass/fail code. It prints whether items came out in order.
- The hazard-pointer queue allocates one node per push. Pooling nodes or storing items in blocks
  (moodycamel's approach) is the obvious next step.
- `MPMC_test.cpp` supports at most 64 threads (`MAX_THREADS`).
- Everything uses `int` rather than a template type `T`.
- Each benchmark result is the time for a whole 1M-item run, not the latency of a single push or
  pop.

## Reference

Anthony Williams, *C++ Concurrency in Action* (2nd ed.): ch. 4 (synchronizing operations), ch. 6
(lock-based data structures), ch. 7 (lock-free data structures).
Maged Michael & Michael Scott, *Simple, Fast, and Practical Non-Blocking and Blocking Concurrent
Queue Algorithms* (1996).
