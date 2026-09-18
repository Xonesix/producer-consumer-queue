#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <ratio>
#include <thread>
#include <vector>
using namespace std;

class lock_free_queue
{
    private:
        struct node;

        // Using split reference technique, we'll bundle counts in 1 struct to make count and claims atomic
        struct counted_node_ptr
        {
            int external_count;
            node* ptr;
        };
        std::atomic<counted_node_ptr> head;
        std::atomic<counted_node_ptr> tail;

        struct node_counter
        {
            unsigned internal_count:30;
            unsigned external_counters:2;
        };

        struct node
        {
            std::atomic<int*> data;
            atomic<node_counter> count;
            atomic<counted_node_ptr> next;

            node()
            {
                node_counter new_count;
                new_count.internal_count = 0;
                new_count.external_counters = 2; // head and tail both start out referencing this node
                count.store(new_count);

                counted_node_ptr next_val;
                next_val.ptr = nullptr;
                next_val.external_count = 0;
                next.store(next_val);

                data.store(nullptr);
            }

            void release_ref()
            {
                node_counter old_counter=count.load(std::memory_order_relaxed);
                node_counter new_counter;
                do {
                    new_counter = old_counter;
                    --new_counter.internal_count;
                } while (!count.compare_exchange_strong(old_counter, new_counter, std::memory_order_acquire, std::memory_order_relaxed));
                // only delete once BOTH internal and external counts have hit zero -
                // otherwise some other thread still has a live claim on this node
                if(!new_counter.internal_count && !new_counter.external_counters)
                {
                    delete this;
                }
            }
        };

        static void increase_external_count(
            atomic<counted_node_ptr>& counter, counted_node_ptr& old_counter)
        {
            counted_node_ptr new_counter;
            do
            {
                new_counter=old_counter;
                ++new_counter.external_count;
            } while(!counter.compare_exchange_strong(old_counter, new_counter, std::memory_order_acquire, std::memory_order_relaxed));
            old_counter.external_count=new_counter.external_count; // obtain reference and try to increase the head count
        }

        static void free_external_counter(counted_node_ptr &old_node_ptr)
        {
            node* const ptr = old_node_ptr.ptr;
            int const count_increase=old_node_ptr.external_count-2;
            node_counter old_counter=ptr->count.load(std::memory_order_relaxed);
            node_counter new_counter;
            do {
                new_counter = old_counter;
                --new_counter.external_counters;
                new_counter.internal_count+=count_increase;
            } while (!ptr->count.compare_exchange_strong(old_counter, new_counter, std::memory_order_acquire, std::memory_order_relaxed));
            if (!new_counter.internal_count && !new_counter.external_counters)
                delete ptr;
        }

    public:
        lock_free_queue()
        {
            counted_node_ptr initial;
            initial.ptr = new node;
            initial.external_count = 1; // one reference from head, one from tail
            head.store(initial);
            tail.store(initial);
        }

        ~lock_free_queue()
        {
            // while head is not nullptr (alias old_head) remove node
            while(node* const old_head=head.load().ptr)
            {
                head.store(old_head->next.load());
                delete old_head;
            }
        }

        unique_ptr<int> pop()
        {
            counted_node_ptr old_head = head.load(std::memory_order_relaxed); // load old head before looping
            for(;;)
            {
                increase_external_count(head, old_head); // claim a reference BEFORE touching the node - this was the missing piece
                node* const ptr = old_head.ptr;
                if (ptr == tail.load().ptr) // if head == tail, release and return null because q is empty
                {
                    ptr->release_ref();
                    return unique_ptr<int>();
                }
                counted_node_ptr next=ptr->next.load();
                if(head.compare_exchange_strong(old_head, next)) // otherwise try to claim the data - use the 'next' we already loaded
                {
                    int* const res=ptr->data.exchange(nullptr);
                    free_external_counter(old_head); // if claimed release external references | once released the node can be deleted
                    return unique_ptr<int>(res);
                }
                ptr->release_ref(); // if CAS fails then release the reference ptr
            }
        }

        // Push revision
        void push(int newVal)
        {
            unique_ptr<int> new_data(new int(newVal));
            counted_node_ptr new_next;
            new_next.ptr = new node;
            new_next.external_count=1;
            counted_node_ptr old_tail=tail.load();
            for(;;)
            {
                increase_external_count(tail, old_tail);
                int* old_data = nullptr;
                if (old_tail.ptr->data.compare_exchange_strong(old_data, new_data.get()))
                {
                    old_tail.ptr->next=new_next;
                    old_tail=tail.exchange(new_next);
                    free_external_counter(old_tail);
                    new_data.release();
                    break;
                }
                old_tail.ptr->release_ref();
            }
        }
};

/*
 * Well what if we had dummy nodes betweeb real nodes, so when multiple threads try to change tail nodes, they oly need to update the tail node?
 * Or we could make data ptr atomic, if call succeeds that'we claim that node and add a tail.
 *
 *
 */

// void example_push_in_stack() {
//     node* const new_node = new node()
//     new_node->next = head.load();
//     while (!head.compare_exchange_weak(new_node->next, new_node))
// }


// pop with compare exchange
/*
    node* old_head = head.load()
    while(!head.compare_exchange_weak(old_head, old_head->next))
    result = old_head->data; // result means this is a copy!! Which can throw an exception || then the thread leaves the node hanging VERY BAD || maybe pass in a smart ptr?
    if you do shared ptr for result, the allocation for shared can throw as well!

IF we allocate the data in push itself

So data is shared_ptr!
    struct node
        shared_ptr node;

        node(T const& data)
            data(make_shared<T>(data_))

Ok so we can try making a garbage colector || clean up when no threads are calling pop

Use counter to count how many nodes

atomic<int> threads_in_pop

pop()
...
++threads in pop;
old_head = head.load

if oldhead
    res.swap(old_head->data) to avoid exception
try_reclaim(old_head)

try_reclaim(node* old_head)
    if threads_in_pop == 1
    nodes_to_delete = to_be_deleted.exchange()

    add a hazard pointer for pop

    pop()
    ...
    std::atomic<void*>& hp=get_hp_currThread()


    soo

    do {
        temp = old_head
        hazard.store(old_head) Store hold head in hazarad | Now no thread can delete it while using it
        old_head = head.load() You can now load it

    } while (old_head != temp) While to confirm assign happens || because if thread deletes it it will break


 */
