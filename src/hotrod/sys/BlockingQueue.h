#ifndef ISPN_HOTROD_BLOCKINGQUEUE_H
#define ISPN_HOTROD_BLOCKINGQUEUE_H

#include <list>
#include <algorithm>
#include "hotrod/sys/Condition.h"
#include "hotrod/sys/Mutex.h"
#include "infinispan/hotrod/exceptions.h"

namespace infinispan {
namespace hotrod {
namespace sys {

template<class T> class BlockingQueue {
public:
    int onWait = 0;
    BlockingQueue<T>(size_t capacity_) : capacity(capacity_)
    {
    }

    BlockingQueue<T>() :
            capacity(0) {
    }

    void push(T const& value) {
        ScopedLock<Mutex> l(lock);

        if (capacity > 0 && queue.size() == capacity) {
            throw Exception("Queue is full");
        }
        queue.push_back(value);
        condition.notify();
    }

    bool offer(T const& value) {
        ScopedLock<Mutex> l(lock);
        if (capacity > 0 && queue.size() == capacity) {
            return false;
        }
        queue.push_back(value);
        condition.notify();
        return true;
    }

    T pop() {
        ScopedLock<Mutex> l(lock);

        if (queue.empty()) {
            ++onWait;
            condition.wait(lock);
            --onWait;
        }
        if (queue.empty()) {
            // thread awaken by a notify, raise exception 
            throw infinispan::hotrod::NoSuchElementException("Reached maximum number of connections");
        }
        T element = queue.front();
        queue.pop_front();
        return element;
    }

    T popOrThrow() {
        ScopedLock<Mutex> l(lock);

        if (queue.empty()) {
            throw infinispan::hotrod::NoSuchElementException("Reached maximum number of connections");
        }
        T element = queue.front();
        queue.pop_front();
        return element;
    }

    bool poll(T& value) {
        ScopedLock<Mutex> l(lock);

        if (queue.empty()) {
            return false;
        } else {
            value = queue.front();
            queue.pop_front();
            return true;
        }
    }

    bool remove(T const& value) {
        ScopedLock<Mutex> l(lock);

        size_t old_size = queue.size();
        queue.remove(value);
        return old_size != queue.size();
    }

    size_t size() {
        ScopedLock<Mutex> l(lock);

        return queue.size();
    }

    void notifyAll() {
        condition.notifyAll();
    }

private:
    size_t capacity;
    std::list<T> queue;
    Mutex lock;
    Condition condition;

};

}}} // namespace infinispan::hotrod::sys

#endif  /* ISPN_HOTROD_BLOCKINGQUEUE_H */
