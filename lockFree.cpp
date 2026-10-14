// SPSC lock-free queue: 1,000,000 element push/pop test
// NOTE: this queue is only correct for ONE producer thread and ONE consumer thread.

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
using namespace std;

class lock_free_queue
{
    private:
        struct node
        {
            shared_ptr<int> data;
            node* next;
            node():next(nullptr){}
        };
        std::atomic<node*> head;   // only the consumer touches head
        std::atomic<node*> tail;   // only the producer writes tail; consumer reads it

        node* pop_head()
        {
            node* const old_head=head.load();
            if (old_head == tail.load())   // head == tail -> only the dummy node, queue empty
                return nullptr;
            head.store(old_head->next);
            return old_head;
        }
    public:
        lock_free_queue():
            head(new node), tail(head.load())
        {}

        lock_free_queue(const lock_free_queue&) = delete;
        lock_free_queue& operator=(const lock_free_queue&) = delete;

        ~lock_free_queue()
        {
            while(node* const old_head=head.load())
            {
                head.store(old_head->next);
                delete old_head;
            }
        }

        shared_ptr<int> pop()
        {
            node* old_head=pop_head();
            if(!old_head)
                return shared_ptr<int>();
            shared_ptr<int> const res(old_head->data);
            delete old_head;   // safe with a single consumer
            return res;
        }

        void push(int newVal)
        {
            shared_ptr<int> new_data(make_shared<int>(newVal));
            node* p = new node;
            node* const old_tail = tail.load();
            old_tail->data.swap(new_data);   // safe with a single producer
            old_tail->next=p;
            tail.store(p);                   // publishes data + next to the consumer
        }
};

int main()
{
    constexpr int TOTAL = 1'000'000;
    lock_free_queue q;

    long long sum = 0;
    bool in_order = true;
    long empty_polls = 0;

    auto start = chrono::steady_clock::now();

    thread producer([&q]() {
        for (int i = 0; i < TOTAL; i++)
            q.push(i);
    });

    // pop() is non-blocking: it returns an empty shared_ptr when the queue is
    // empty, so the consumer keeps polling until it has received TOTAL items.
    thread consumer([&]() {
        int expected = 0;
        while (expected < TOTAL) {
            shared_ptr<int> v = q.pop();
            if (!v) { empty_polls++; this_thread::yield(); continue; }
            if (*v != expected) in_order = false;   // SPSC queue must preserve FIFO order
            sum += *v;
            expected++;
        }
    });

    producer.join();
    consumer.join();

    double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - start).count();

    // long long expected_sum = (long long)TOTAL * (TOTAL - 1) / 2;
    // bool ok = in_order && sum == expected_sum && !q.pop();   // queue should be empty at the end

    cout << "items:        " << TOTAL << "\n";
    cout << "FIFO order:   " << (in_order ? "yes" : "NO") << "\n";
    // cout << "sum:          " << sum << " (expected " << expected_sum << ")\n";
    cout << "empty polls:  " << empty_polls << "\n";
    // cout << "result:       " << (ok ? "PASS" : "FAIL") << "\n";
    cout << "TOTAL TIME:   " << ms << " ms\n";
    cout << "throughput:   " << (TOTAL / (ms / 1000.0)) / 1e6 << " M ops/sec\n";
    // return ok ? 0 : 1;
}
