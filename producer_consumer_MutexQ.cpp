// Unbounded mutex queue: 1,000,000 element push/pop test (10 producers, 10 consumers)

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
using namespace std;

using Clock = chrono::steady_clock;   // steady_clock is the right clock for measuring durations

// one option: use a mutex to push and pull (problem: serializes access to the queue)
struct tSafeQ {
private:
    deque<int> base_queue;
    mutex m;
    condition_variable emptyQ;
public:
    void push(int num)
    {
        {
            lock_guard<mutex> g(m);
            base_queue.push_back(num);
        }
        emptyQ.notify_one();   // notify after unlocking so the woken consumer doesn't block on m
    }

    pair<int, Clock::time_point> pop()
    {
        unique_lock<mutex> lk(m);
        emptyQ.wait(lk, [this]() { return !base_queue.empty(); });
        int res = base_queue.front();
        base_queue.pop_front();
        return {res, Clock::now()};
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

    auto tec = make_shared<tSafeQ>();
    vector<thread> producers, consumers;

    array<atomic<long>, NUM_PRODUCERS>   counts{};      // times each value 0-9 was consumed
    array<atomic<double>, NUM_PRODUCERS> last_seen{};   // ms since start when each value was last consumed

    auto tp = Clock::now();

    auto produce = [tec](int x) {
        for (long i = 0; i < PER_PRODUCER; i++)
            tec->push(x);
    };

    // Each consumer pops a fixed share; total pushes == total pops, so all consumers finish.
    auto consume = [tp, tec, &counts, &last_seen]() {
        array<long, NUM_PRODUCERS>   local_count{};
        array<double, NUM_PRODUCERS> local_last{};
        for (long i = 0; i < PER_CONSUMER; i++) {
            auto [val, when] = tec->pop();
            local_count[val]++;
            double ms = chrono::duration<double, milli>(when - tp).count();
            if (ms > local_last[val]) local_last[val] = ms;
        }
        for (int v = 0; v < NUM_PRODUCERS; v++) {
            counts[v].fetch_add(local_count[v], memory_order_relaxed);
            // atomic max: retry until our value is stored or a later time is already there
            double cur = last_seen[v].load();
            while (local_last[v] > cur && !last_seen[v].compare_exchange_weak(cur, local_last[v])) {}
        }
    };

    for (int i = 0; i < NUM_PRODUCERS; i++) producers.emplace_back(produce, i);
    for (int i = 0; i < NUM_CONSUMERS; i++) consumers.emplace_back(consume);

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    double total_ms = chrono::duration<double, milli>(Clock::now() - tp).count();

    bool ok = true;
    long total = 0;
    cout << "counts per value:        ";
    for (int v = 0; v < NUM_PRODUCERS; v++) {
        long c = counts[v].load();
        total += c;
        cout << c << " ";
        if (c != PER_PRODUCER) ok = false;
    }
    cout << "\nlast consumed at (ms):   ";
    for (int v = 0; v < NUM_PRODUCERS; v++) cout << last_seen[v].load() << " ";
    cout << "\ntotal popped:            " << total << " / " << TOTAL << "\n";
    if (total != TOTAL) ok = false;
    cout << "result:                  " << (ok ? "PASS" : "FAIL") << "\n";
    cout << "TOTAL TIME:              " << total_ms << " ms\n";
    cout << "throughput:              " << (TOTAL / (total_ms / 1000.0)) / 1e6 << " M ops/sec\n";
    return ok ? 0 : 1;
}
