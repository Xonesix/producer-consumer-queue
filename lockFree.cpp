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
        struct node
        {
            shared_ptr<int> data;
            node* next;
            node():next(nullptr){}
        };
        std::atomic<node*> head;
        std::atomic<node*> tail;

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

        shared_ptr<int> pop()
        {
            node* old_head=pop_head();
            if(!old_head)
            {
                return shared_ptr<int>();
            }
            shared_ptr<int> const res(old_head->data);
            delete old_head; // both threads will try to delete this node || threads have race condition again
            return res;
        }

        void push(int newVal)
        {
            shared_ptr<int> new_data(make_shared<int>(newVal));
            node* p = new node;
            node* const old_tail = tail.load();
            // what if another thread comes in before this? And this thread is sleeping!
            old_tail->data.swap(new_data);
            old_tail->next=p; // this would be a data race || if the thread is running concurrently
            tail.store(p);
        }




};
