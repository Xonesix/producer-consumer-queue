// Lock-free MPMC queue (Michael & Scott 1996) + hazard pointers: 1,000,000 element test
// Usage: lfmpmc [producers consumers]   (default 10 10)

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
using namespace std;

constexpr int MAX_THREADS = 64;                 // max threads that ever touch the queue
constexpr int RETIRE_SCAN = 2 * MAX_THREADS * 2; // scan retired list when it reaches this size

// small per-thread id (0..MAX_THREADS-1), assigned on first use
inline int thread_slot()
{
    static atomic<int> next_id{0};
    thread_local int id = next_id.fetch_add(1);
    assert(id < MAX_THREADS && "raise MAX_THREADS");
    return id;
}

class lock_free_queue
{
    private:
        struct node
        {
            int data;
            atomic<node*> next;
            node(int v = 0) : data(v), next(nullptr) {}
        };

        // Per-thread hazard pointers + retired list, one cache line each.
        struct alignas(64) slot
        {
            atomic<node*> hp[2] = {nullptr, nullptr};  // nodes this thread is currently reading
            vector<node*> retired;                       // removed nodes waiting to be freed
        };

        alignas(64) atomic<node*> head;
        alignas(64) atomic<node*> tail;
        slot slots[MAX_THREADS];

        // Publish a hazard pointer for src's current value. The re-check ensures the
        // node was still in the queue when we marked it, so nobody can free it now.
        node* protect(atomic<node*>& src, int which, slot& s)
        {
            node* p = src.load();
            while (true) {
                s.hp[which].store(p);
                node* again = src.load();
                if (again == p) return p;
                p = again;
            }
        }

        void retire(node* n, slot& s)
        {
            s.retired.push_back(n);
            if ((int)s.retired.size() < RETIRE_SCAN) return;

            // collect every hazard pointer in use, free retired nodes nobody is reading
            vector<node*> hazards;
            hazards.reserve(MAX_THREADS * 2);
            for (auto& o : slots)
                for (auto& h : o.hp)
                    if (node* p = h.load()) hazards.push_back(p);
            sort(hazards.begin(), hazards.end());

            vector<node*> keep;
            for (node* r : s.retired) {
                if (binary_search(hazards.begin(), hazards.end(), r)) keep.push_back(r);
                else delete r;
            }
            s.retired.swap(keep);
        }

    public:
        lock_free_queue() { node* d = new node; head.store(d); tail.store(d); }
        lock_free_queue(const lock_free_queue&) = delete;
        lock_free_queue& operator=(const lock_free_queue&) = delete;

        ~lock_free_queue()   // no other threads may be using the queue now
        {
            for (node* n = head.load(); n; ) { node* nx = n->next.load(); delete n; n = nx; }
            for (auto& s : slots) for (node* r : s.retired) delete r;
        }

        void push(int v)
        {
            slot& s = slots[thread_slot()];
            node* n = new node(v);
            while (true) {
                node* t = protect(tail, 0, s);
                node* next = t->next.load(memory_order_acquire);
                if (t != tail.load()) continue;                 // tail moved, retry
                if (next == nullptr) {
                    // link n after the last node; this CAS is the actual enqueue
                    if (t->next.compare_exchange_weak(next, n, memory_order_release, memory_order_relaxed)) {
                        tail.compare_exchange_strong(t, n);     // swing tail (ok if someone else did)
                        break;
                    }
                } else {
                    tail.compare_exchange_strong(t, next);      // tail lagging: help, don't wait
                }
            }
            s.hp[0].store(nullptr);
        }

        bool try_pop(int& out)                                  // non-blocking: false when empty
        {
            slot& s = slots[thread_slot()];
            while (true) {
                node* h = protect(head, 0, s);
                node* t = tail.load();
                node* next = h->next.load(memory_order_acquire);
                s.hp[1].store(next);                            // protect next before reading its data
                if (h != head.load()) continue;                 // head moved, retry
                if (next == nullptr) {                          // only the dummy: empty
                    s.hp[0].store(nullptr); s.hp[1].store(nullptr);
                    return false;
                }
                if (h == t) {                                   // tail lagging behind: help it
                    tail.compare_exchange_strong(t, next);
                    continue;
                }
                int v = next->data;                             // read before CAS; next is protected
                if (head.compare_exchange_strong(h, next)) {    // this CAS is the actual dequeue
                    s.hp[0].store(nullptr); s.hp[1].store(nullptr);
                    retire(h, s);                               // old dummy; freed once unprotected
                    out = v;
                    return true;
                }
            }
        }
};

int main(int argc, char** argv)
{
    const int  NUM_PRODUCERS = argc > 2 ? atoi(argv[1]) : 10;
    const int  NUM_CONSUMERS = argc > 2 ? atoi(argv[2]) : 10;
    const long TOTAL         = 1'000'000;
    const long PER_PRODUCER  = TOTAL / NUM_PRODUCERS;
    const long PER_CONSUMER  = TOTAL / NUM_CONSUMERS;
    if (TOTAL % NUM_PRODUCERS || TOTAL % NUM_CONSUMERS || NUM_PRODUCERS + NUM_CONSUMERS > MAX_THREADS) {
        cerr << "thread counts must divide 1,000,000 and total <= " << MAX_THREADS << "\n";
        return 2;
    }

    lock_free_queue q;
    atomic<long long> sum{0};
    atomic<long> popped{0}, empty_polls{0};
    atomic<bool> order_ok{true};

    vector<thread> producers, consumers;
    auto start = chrono::steady_clock::now();

    for (int p = 0; p < NUM_PRODUCERS; p++)
        producers.emplace_back([&q, p, PER_PRODUCER]() {
            for (long s = 0; s < PER_PRODUCER; s++)
                q.push((int)(p * PER_PRODUCER + s));
        });

    for (int c = 0; c < NUM_CONSUMERS; c++)
        consumers.emplace_back([&]() {
            vector<long> last(NUM_PRODUCERS, -1);   // each consumer must see a producer's items in order
            long long local_sum = 0;
            long got = 0, local_empty = 0;
            bool ok = true;
            int v;
            while (got < PER_CONSUMER) {
                if (!q.try_pop(v)) { local_empty++; this_thread::yield(); continue; }
                long p = v / PER_PRODUCER, s = v % PER_PRODUCER;
                if (s <= last[p]) ok = false;
                last[p] = s;
                local_sum += v;
                got++;
            }
            sum += local_sum; popped += got; empty_polls += local_empty;
            if (!ok) order_ok = false;
        });

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - start).count();

    int leftover;
    long long expected_sum = (long long)TOTAL * (TOTAL - 1) / 2;
    bool ok = order_ok && popped == TOTAL && sum == expected_sum && !q.try_pop(leftover);

    cout << "producers/consumers: " << NUM_PRODUCERS << "/" << NUM_CONSUMERS << "\n";
    cout << "total popped: " << popped << " / " << TOTAL << "\n";
    cout << "per-producer FIFO: " << (order_ok ? "yes" : "NO") << "\n";
    cout << "sum:          " << sum << " (expected " << expected_sum << ")\n";
    cout << "empty polls:  " << empty_polls << "\n";
    cout << "result:       " << (ok ? "PASS" : "FAIL") << "\n";
    cout << "TOTAL TIME:   " << ms << " ms\n";
    cout << "throughput:   " << (TOTAL / (ms / 1000.0)) / 1e6 << " M ops/sec\n";
    return ok ? 0 : 1;
}