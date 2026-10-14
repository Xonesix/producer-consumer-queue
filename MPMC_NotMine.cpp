// moodycamel::ConcurrentQueue: 1,000,000 element push/pop test
// Usage: mc_bench [spsc|mpmc]   (no argument runs both)

#include "concurrentqueue.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
using namespace std;

constexpr long TOTAL = 1'000'000;

bool run(int num_producers, int num_consumers)
{
    const long per_producer = TOTAL / num_producers;
    const long per_consumer = TOTAL / num_consumers;

    moodycamel::ConcurrentQueue<int> q;
    atomic<long long> sum{0};
    atomic<long> popped{0}, empty_polls{0};
    atomic<bool> in_order{true};   // only checked for 1 producer / 1 consumer

    vector<thread> producers, consumers;
    auto start = chrono::steady_clock::now();

    for (int p = 0; p < num_producers; p++)
        producers.emplace_back([&q, p, per_producer]() {
            for (long s = 0; s < per_producer; s++)
                q.enqueue((int)(p * per_producer + s));
        });

    for (int c = 0; c < num_consumers; c++)
        consumers.emplace_back([&, per_consumer]() {
            long long local_sum = 0;
            long local_empty = 0, got = 0;
            int item, expected = 0;
            while (got < per_consumer) {
                if (!q.try_dequeue(item)) {   // non-blocking: false when empty
                    local_empty++;
                    this_thread::yield();
                    continue;
                }
                if (num_producers == 1 && num_consumers == 1 && item != expected++)
                    in_order = false;
                local_sum += item;
                got++;
            }
            sum += local_sum;
            popped += got;
            empty_polls += local_empty;
        });

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - start).count();

    int leftover;
    long long expected_sum = (long long)TOTAL * (TOTAL - 1) / 2;
    bool ok = in_order && popped == TOTAL && sum == expected_sum && !q.try_dequeue(leftover);

    cout << "=== " << num_producers << " producer(s), " << num_consumers << " consumer(s) ===\n";
    cout << "total popped: " << popped << " / " << TOTAL << "\n";
    if (num_producers == 1 && num_consumers == 1)
        cout << "FIFO order:   " << (in_order ? "yes" : "NO") << "\n";
    cout << "sum:          " << sum << " (expected " << expected_sum << ")\n";
    cout << "empty polls:  " << empty_polls << "\n";
    cout << "result:       " << (ok ? "PASS" : "FAIL") << "\n";
    cout << "TOTAL TIME:   " << ms << " ms\n";
    cout << "throughput:   " << (TOTAL / (ms / 1000.0)) / 1e6 << " M ops/sec\n\n";
    return ok;
}

int main(int argc, char** argv)
{
    bool do_spsc = argc < 2 || strcmp(argv[1], "spsc") == 0;
    bool do_mpmc = argc < 2 || strcmp(argv[1], "mpmc") == 0;
    bool ok = true;
    if (do_spsc) ok &= run(1, 1);
    if (do_mpmc) ok &= run(10, 10);
    return ok ? 0 : 1;
}
