// basic implementation of console in out
//


#include <array>
#include <atomic>
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
    vector<thread> producers = {};
    vector<thread> consumers = {};

    // use atomic array to keep track, print all vals |at end
    std::array<std::atomic<int>, 10> atomic_array{};


    auto produce = ([tec](int x){
        tec->push(x);

    });

    auto consume = ([tec, &atomic_array](){
        // need a way to write what we have consumed || time doesn't matter so an atomic way to measure this would be great
        // atomic add to tarray
        atomic_array[*tec->pop()]++;
    });
    for (int i = 0; i < 10; i++)
    {
            // unambiguously produces 0-9
            thread t(produce, i);
            producers.push_back(std::move(t));
    }

    for (int i = 0; i < 10; i++)
    {
        thread t(consume);
        consumers.push_back(std::move(t));
    }


    for (thread& t: producers)
    {
        t.join();
    }
    for (thread &t: consumers)
    {
        t.join();
    }

    // print out produced vals
    // 
    for (auto &elem: atomic_array)
    {
        cout << elem << " ";
    }
    cout << endl;

    return 0;
}
