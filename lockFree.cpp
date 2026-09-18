#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <ratio>
#include <thread>
#include <vector>
#include <mutex>
#include <memory>
using namespace std;

class lock_free_queue
{
    private:
        struct node;

        // Using splti reference technique, we'll bundle counts in 1 struct to make count and claims atomic
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
            std::atomic<int> data;
            atomic<node_counter> count;
            counted_node_ptr next;
            node()
            {
                node_counter new_count;
                new_count.internal_count = 0;
                new_count.external_counters = 2;
                count.store(new_count);
                next.ptr=nullptr;
                next.external_count=0;
            }
        };

        
        node* pop_head()
        {
            node* const old_head=head.load();

            if (old_head == tail.load()) // if the head and tail are same, then no more nodes, hence nullptr
                return nullptr;
            head.store(old_head->next);
            return old_head;
        }
        // loading atomically into here
    public:
        lock_free_queue():
            head(new node), tail(head.load())
        {}

        ~lock_free_queue()
        {
            // while head.load() is not nullptr (alias old_head) remove node
            while(node* const old_head=head.load())
            {
                head.store(old_head->next);
                delete old_head;
            }
        }

        unique_ptr<int> pop()
        {
            counted_node_ptr old_head = head.load(std::memory_order_relaxed); // load old head before looping
            for(;;)
            {
                node* const ptr = old_head.ptr; // increase count of loaded val
                if (ptr == tail.load().ptr) // if head == tail Release and return null because q is empty
                {
                    ptr->release_ref();
                    return unique_ptr<int>();
                }
                if(head.compare_exchange_strong(old_head,ptr->next)) // otherwise try to claim the data
                {
                    int const res=ptr->data.exchange(nullptr);
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