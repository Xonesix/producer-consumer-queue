// basic implementation of console in out
//


#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <mutex>
#include <memory>
using namespace std;
// one option use mutex to push and pull (problem: kills concurrency, essentially sequential code)
struct tSafeQ {
private:
    vector<int> base_queue;
    mutex m;
public:

    tSafeQ()
    {
        base_queue = {};
    }
    
    void push(int num)
    {
        lock_guard<mutex> g(m);
        base_queue.push_back(std::move(num)); // we don't want to copy the value all the time
    }

    shared_ptr<int> pop()
    {
        
        lock_guard<mutex> g(m);
        if (base_queue.empty())
        {
            return nullptr;
        }
        const shared_ptr<int> res(make_shared<int>(base_queue.back())); // what does the const mean? i think you can't mutate it
        base_queue.pop_back();
        return res;
        
    }
};

int main()
{
    shared_ptr<tSafeQ> tec(new tSafeQ());
    vector<thread> ts = {};

    auto produce = ([tec](int x){
        tec->push(x);
        
    });

    auto consume = ([tec](){
        cout << "Popped: " << *tec->pop() << endl;
    });
    for (int i = 0; i < 20; i++)
    {
        if (i % 2 == 0)
        {
            //produce
            thread t(produce, i);
            ts.push_back(std::move(t));
        }

        else 
        {
            // consume
            thread t(consume);
            ts.push_back(std::move(t));
        }
    }

    for (thread& t: ts)
    {
        t.join();
    }
    
    return 0;
}
