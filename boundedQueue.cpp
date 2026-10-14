// Bounded thread-safe queue: 1,000,000 element push/pop test

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
using namespace std;

struct tSafeQ {
private:
    size_t capacity;
    deque<int> base_queue;
    mutex m;
    condition_variable emptyQ;   // consumers wait here when queue is empty
    condition_variable fullQ;    // producers wait here when queue is full
public:
    explicit tSafeQ(size_t cap = 10000) : capacity(cap) {}

    void push(int num)
    {
        unique_lock<mutex> lk(m);
        fullQ.wait(lk, [this]() { return base_queue.size() < capacity; });
        base_queue.push_back(num);
        lk.unlock();               // notify outside the lock so the woken thread doesn't immediately block
        emptyQ.notify_one();
    }

    int pop()
    {
        unique_lock<mutex> lk(m);
        emptyQ.wait(lk, [this]() { return !base_queue.empty(); });
        int res = base_queue.front();
        base_queue.pop_front();
        lk.unlock();
        fullQ.notify_one();
        return res;
    }
};

int main()
{
    constexpr int  NUM_PRODUCERS = 10;
    constexpr int  NUM_CONSUMERS = 10;
    constexpr long TOTAL         = 1'000'000;
    constexpr long PER_PRODUCER  = TOTAL / NUM_PRODUCERS;   // 100,000 each
    constexpr long PER_CONSUMER  = TOTAL / NUM_CONSUMERS;   // 100,000 each
    static_assert(TOTAL % NUM_PRODUCERS == 0 && TOTAL % NUM_CONSUMERS == 0,
                  "TOTAL must divide evenly across threads");

    auto tec = make_shared<tSafeQ>(10000);
    vector<thread> producers, consumers;

    // how many times each value (0-9) was consumed; should end at PER_PRODUCER each
    array<atomic<long>, NUM_PRODUCERS> counts{};
    atomic<long> total_popped{0};

    auto tp = chrono::steady_clock::now();

    auto produce = [tec](int x) {
        for (long i = 0; i < PER_PRODUCER; i++)
            tec->push(x);
    };

    // Each consumer pops a fixed share. Since total pushes == total pops,
    // every consumer is guaranteed to finish (no extra stop signal needed).
    auto consume = [tec, &counts, &total_popped]() {
        array<long, NUM_PRODUCERS> local{};          // tally locally to avoid atomic contention
        for (long i = 0; i < PER_CONSUMER; i++)
            local[tec->pop()]++;
        for (int v = 0; v < NUM_PRODUCERS; v++)
            counts[v].fetch_add(local[v], memory_order_relaxed);
        total_popped.fetch_add(PER_CONSUMER, memory_order_relaxed);
    };

    for (int i = 0; i < NUM_PRODUCERS; i++) producers.emplace_back(produce, i);
    for (int i = 0; i < NUM_CONSUMERS; i++) consumers.emplace_back(consume);

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - tp).count();

    // verify
    bool ok = total_popped == TOTAL;
    cout << "counts per value: ";
    for (int v = 0; v < NUM_PRODUCERS; v++) {
        long c = counts[v].load();
        cout << c << " ";
        if (c != PER_PRODUCER) ok = false;
    }
    cout << "\n";
    cout << "total popped:      " << total_popped << " / " << TOTAL << "\n";
    cout << "result:            " << (ok ? "PASS" : "FAIL") << "\n";
    cout << "TOTAL TIME:        " << ms << " ms\n";
    cout << "throughput:        " << (TOTAL / (ms / 1000.0)) / 1e6 << " M ops/sec\n";
    return ok ? 0 : 1;
}
