#pragma once
#include "util.h"
#include <atomic>
#include <mutex>
#include <semaphore.h>
#include <immintrin.h>
#include <list>
#include <algorithm>

using namespace std;
// #define MUTEX

// #define HYBRID_SPIN_LOCK
#if defined(MUTEX)
class Lock2 {
    mutex l;
public:
    void lock() {
        l.lock();
    }
    void unlock(){
        l.unlock();
    }
};
#elif defined(HYBRID_FUTEX)

class Lock {
    atomic<int> lockInfo{0};
    mutex l;
public:
    void lock() {
        int spinStart = 4;
        int endSpin = 1024;
        for(;;) {
            int exp = 0;
            if (lockInfo.compare_exchange_strong(exp, 1)) return;
            if(exp == 2) break;
            for(int i = 0; i < spinStart; i++) _mm_pause();
            if(spinStart < endSpin) spinStart += spinStart;
            else break;
        }
        
        int ycnt = 0;
        int scnt = 0;
        int spin = 4;
        bool flag = false;
        for (;;) {
            if (lockInfo.exchange(2) == 0) {
                flag = true;
                break;
            }
            if (scnt < 16) {
                for (int i = 0; i < spin; i += 1) _mm_pause();
                if (spin < endSpin) spin += spin;
                else scnt += 1;
            } else if (ycnt < 0) {
                ycnt += 1;
                // this_thread::yield();
            } else {
                break;
            }
        }

        if (flag) {
            return;
        }else {
            l.lock(); // sleep
            lock();
        }
    }

    void unlock() {
        lockInfo = 0;
        l.unlock();
    }
};

#elif defined(HYBRID_SPIN_LOCK)
class Lock2 {
    atomic<int> l;
    int maxSpin = 5;
public:
    void lock() {
        int expected = 0;
        int mSpin = maxSpin;
        while (!l.compare_exchange_strong(expected, 1, memory_order_acquire)) {
            expected = 0;
            for(int i = 0; i < mSpin; i++)
                _mm_pause();
            
            mSpin += mSpin;
        }
    }

    void unlock() {
        l.store(0, memory_order_release);
    }

};


#elif defined(HYBRID_MUTEX) 
class Lock2 {
    atomic<int> lockInfo{0};
    mutex l;
    int maxSpin = 2;
    // I HAVE A PAD IN my struct.
public:
    void lock() {
        lockInfo.fetch_add(1, memory_order_relaxed);
        while(1) {
            if (lockInfo >= 2){ // contented Case
                for(int i = 0; i < maxSpin; i++) {
                    _mm_pause();
                }
                if(l.try_lock()) {
                    return;
                }else {
                    maxSpin += maxSpin;
                    if (maxSpin > 1024) {
                        this_thread::yield(); // sleep
                    }
                }
            }else {
                l.lock();
                return;
            }
        }
    }

    void unlock() {
        lockInfo.fetch_sub(1, memory_order_relaxed);
        l.unlock();
    }
};
#else 
class Lock2 {
    pthread_spinlock_t l;
public:
    Lock2() {
        pthread_spin_init(&l, PTHREAD_PROCESS_PRIVATE);
    }
    void lock() {
        pthread_spin_lock(&l);
    }
    void unlock(){
        pthread_spin_unlock(&l);
    }
};
#endif
class AlgorithmAA
{
public:
    static constexpr int TOMBSTONE = -1;
    static constexpr int NULL_VAL = -2;

    char padding0[PADDING_BYTES];
    const int numThreads;
    int capacity;
    char padding2[PADDING_BYTES];
    
    struct PaddedIntLocked
    {
        int key;
        Lock2 l{};
        list<int> *ll;
        char padding[PADDING_BYTES - sizeof(key) - sizeof(l) - sizeof(ll)];
        PaddedIntLocked(): key(NULL_VAL) {
            ll = new list<int>;
        };
    };

    PaddedIntLocked *data;

public:
    AlgorithmAA(const int _numThreads, const int _capacity);
    ~AlgorithmAA();
    bool insertIfAbsent(const int tid, const int &key);
    bool erase(const int tid, const int &key);
    long getSumOfKeys();
    void printDebuggingDetails();
};

/**
 * constructor: initialize the hash table's internals
 *
 * @param _numThreads maximum number of threads that will ever use the hash table (i.e., at least tid+1, where tid is the largest thread ID passed to any function of this class)
 * @param _capacity is the INITIAL size of the hash table (maximum number of elements it can contain WITHOUT expansion)
 */
AlgorithmAA::AlgorithmAA(const int _numThreads, const int _capacity)
    : numThreads(_numThreads), capacity(_capacity)
{
    data = new PaddedIntLocked[capacity];
    for (int i = 0; i < capacity; i++)
        data[i].key = NULL_VAL;
}

// destructor: clean up any allocated memory, etc.
AlgorithmAA::~AlgorithmAA()
{
    delete[] data;
}

// semantics: try to insert key. return true if successful (if key doesn't already exist), and false otherwise
bool AlgorithmAA::insertIfAbsent(const int tid, const int &key)
{
    uint32_t hashedIndex = murmur3(key);
    // for (int i = 0; i < capacity; ++i)
    // {

        uint32_t index = (hashedIndex) % capacity;
        data[index].l.lock();
        int found = key;
        auto it = find(data[index].ll->begin(), data[index].ll->end(), found);
        if( it == data[index].ll->end())
            data[index].ll->push_back(key);
        
        data[index].l.unlock();
    // }

    return false;
}

// semantics: try to erase key. return true if successful, and false otherwise
bool AlgorithmAA::erase(const int tid, const int &key)
{
    uint32_t hashedIndex = murmur3(key);
    
        uint32_t index = (hashedIndex) % capacity;
        data[index].l.lock();
        int found = key;
        auto it = find(data[index].ll->begin(), data[index].ll->end(), found);
        if (it != data[index].ll->end())
            data[index].ll->erase(it);
        
            
        data[index].l.unlock();

    return false;
}

// semantics: return the sum of all KEYS in the set
int64_t AlgorithmAA::getSumOfKeys()
{
    // because this function is called at the end of threads' work.
    // I have not guard it with a lock.
    int64_t keySummation = 0;
    for (int i = 0; i < capacity; i++)
        keySummation += ((data[i].key == NULL_VAL || data[i].key == TOMBSTONE) ? 0 : data[i].key);

    return keySummation;
}

// print any debugging details you want at the end of a trial in this function
void AlgorithmAA::printDebuggingDetails()
{
}
