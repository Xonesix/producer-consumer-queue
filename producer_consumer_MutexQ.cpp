// basic implementation of console in out
//


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
// one option use mutex to push and pull (problem: kills concurrency, essentially sequential code)
struct tSafeQ {
private:
    deque<int> base_queue;
    mutex m;
    condition_variable emptyQ;
public:

    tSafeQ()
    {
        base_queue = {};
    }

    void push(int num)
    {
        lock_guard<mutex> g(m);
        base_queue.push_back(std::move(num)); // we don't want to copy the value all the time
        emptyQ.notify_one();
    }

    pair<int, std::chrono::time_point<std::chrono::system_clock>> pop()
    {

        unique_lock<mutex> lk(m);
        // when there is somethign worth processing, perform task
        emptyQ.wait(lk, [this](){return !base_queue.empty();});


        int res = (base_queue.front()); // what does the const mean? i think you can't mutate it
        base_queue.pop_front();
        return {res, chrono::system_clock::now()};

    }
};

int main()
{
    shared_ptr<tSafeQ> tec(new tSafeQ());
    vector<thread> producers = {};
    vector<thread> consumers = {};

    // use atomic array to keep track, print all vals |at end
    std::array<std::atomic<double>, 10> atomic_array{};

    auto tp = chrono::system_clock::now();

    auto produce = ([tec](int x){
        tec->push(x);

    });

    auto consume = ([tp, tec, &atomic_array](){
        // need a way to write what we have consumed || time doesn't matter so an atomic way to measure this would be great
        // atomic add to tarray
        auto x = tec->pop();
        // what if another thread

        atomic_array[x.first] = chrono::duration_cast<chrono::duration<double, std::milli>>(x.second-tp).count();


    });
    for (int i = 0; i < 10; i++)
    {
            // unambiguously produces 0-9
            thread t(produce, i);
            producers.push_back(std::move(t));
    }


    // What happens when a consume
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
