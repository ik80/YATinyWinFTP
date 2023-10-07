/*
 * LFMPSCQueue.h
 *
 * Code borrowed from blogpost by Dmitry Vyukov: http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
 *
 */

#ifndef LFMPSCQUEUE_H_
#define LFMPSCQUEUE_H_

#include <atomic>
#include <exception>

// TODO: figure out proper noexcept operator syntax to properly apply noexcept to push/pop/moveTo based on T
template<typename T>
class LFMPMCQueue
{
public:
    LFMPMCQueue(size_t maxQueueSize) :
                    elements(new Element[maxQueueSize]), maxQueueSizeMask(maxQueueSize - 1)
    {
        if(!((maxQueueSize >= 2) && ((maxQueueSize & (maxQueueSize - 1)) == 0)))
            abort();

        for(size_t i = 0; i != maxQueueSize; i += 1)
            elements[i].sequence.store(i, std::memory_order_relaxed);
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

    ~LFMPMCQueue()
    {
        delete[] elements;
    }

    bool push(T const& data)
    {
        Element* pElement;
        size_t pos = head.load(std::memory_order_relaxed);
        for(;;)
        {
            pElement = &elements[pos & maxQueueSizeMask];
            size_t seq = pElement->sequence.load(std::memory_order_acquire);
            intptr_t dif = (intptr_t) seq - (intptr_t) pos;
            if(dif == 0)
            {
                if(head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;
            }
            else if(dif < 0)
                return false;
            else
                pos = head.load(std::memory_order_relaxed);
        }
        pElement->data = data;
        pElement->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool moveFrom(T&& data)
    {
        Element* pElement;
        size_t pos = head.load(std::memory_order_relaxed);
        for(;;)
        {
            pElement = &elements[pos & maxQueueSizeMask];
            size_t seq = pElement->sequence.load(std::memory_order_acquire);
            intptr_t dif = (intptr_t) seq - (intptr_t) pos;
            if(dif == 0)
            {
                if(head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;
            }
            else if(dif < 0)
                return false;
            else
                pos = head.load(std::memory_order_relaxed);
        }
        pElement->data = std::move(data);
        pElement->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& data)
    {
        Element* pElement;
        size_t pos = tail.load(std::memory_order_relaxed);
        for(;;)
        {
            pElement = &elements[pos & maxQueueSizeMask];
            size_t seq = pElement->sequence.load(std::memory_order_acquire);
            intptr_t dif = (intptr_t) seq - (intptr_t) (pos + 1);
            if(dif == 0)
            {
                if(tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;
            }
            else if(dif < 0)
                return false;
            else
                pos = tail.load(std::memory_order_relaxed);
        }
        data = pElement->data;
        pElement->sequence.store(pos + maxQueueSizeMask + 1, std::memory_order_release);
        return true;
    }

    bool moveTo(T& data)
    {
        Element* pElement;
        size_t pos = tail.load(std::memory_order_relaxed);
        for(;;)
        {
            pElement = &elements[pos & maxQueueSizeMask];
            size_t seq = pElement->sequence.load(std::memory_order_acquire);
            intptr_t dif = (intptr_t) seq - (intptr_t) (pos + 1);
            if(dif == 0)
            {
                if(tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;
            }
            else if(dif < 0)
                return false;
            else
                pos = tail.load(std::memory_order_relaxed);
        }
        data = std::move(pElement->data);
        pElement->sequence.store(pos + maxQueueSizeMask + 1, std::memory_order_release);
        return true;
    }

private:
    struct Element
    {
        std::atomic_size_t sequence;
        T data;
    };

    static const size_t CACHE_LINE_SIZE = 64;

    Element* elements;
    const size_t maxQueueSizeMask;
    char pad0[CACHE_LINE_SIZE - sizeof(size_t) - sizeof(Element*)];
    std::atomic_size_t head;
    char pad1[CACHE_LINE_SIZE - sizeof(std::atomic_size_t)];
    std::atomic_size_t tail;
    char pad2[CACHE_LINE_SIZE - sizeof(std::atomic_size_t)];

    // deleted functions
    LFMPMCQueue(const LFMPMCQueue&);
    void operator=(const LFMPMCQueue&);
    LFMPMCQueue(LFMPMCQueue&&);
    void operator =(LFMPMCQueue&&);

};

#endif /* LFMPSCQUEUE_H_ */
