#pragma once
#include "util.h"
#include <atomic>
#include <mutex>
#include <semaphore.h>
#include <immintrin.h>
#include <list>

using namespace std;
// #define MUTEX


#if defined(MUTEX)
class Lock {
    mutex l;
public:
    void lock() {
        l.lock();
    }
    void unlock(){
        l.unlock();
    }
};
#elif defined(HYBRID_MUTEX) 
class Lock {
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
                    l.lock();
                    return;
                }else {
                    maxSpin += maxSpin;
                    if (maxSpin > 10) {
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
class Lock {
    pthread_spinlock_t l;
public:
    Lock() {
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
class AlgorithmA
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
        Lock l{};
        char padding[PADDING_BYTES - sizeof(key) - sizeof(l)];
        PaddedIntLocked(): key(NULL_VAL) {};
    };

    PaddedIntLocked *data;

public:
    AlgorithmA(const int _numThreads, const int _capacity);
    ~AlgorithmA();
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
AlgorithmA::AlgorithmA(const int _numThreads, const int _capacity)
    : numThreads(_numThreads), capacity(_capacity)
{
    data = new PaddedIntLocked[capacity];
    for (int i = 0; i < capacity; i++)
        data[i].key = NULL_VAL;
}

// destructor: clean up any allocated memory, etc.
AlgorithmA::~AlgorithmA()
{
    delete[] data;
}

// semantics: try to insert key. return true if successful (if key doesn't already exist), and false otherwise
bool AlgorithmA::insertIfAbsent(const int tid, const int &key)
{
    uint32_t hashedIndex = murmur3(key);
    for (int i = 0; i < capacity; ++i)
    {

        uint32_t index = (hashedIndex + i) % capacity;
        data[index].l.lock();
        int found = data[index].key;

        if (found == NULL_VAL)
        {
            data[index].key = key;
            data[index].l.unlock();
            return true;
        }
        else if (found == key)
        {
            data[index].l.unlock();
            return false;
        }
        data[index].l.unlock();
    }

    return false;
}

// semantics: try to erase key. return true if successful, and false otherwise
bool AlgorithmA::erase(const int tid, const int &key)
{
    uint32_t hashedIndex = murmur3(key);
    for (int i = 0; i < capacity; ++i)
    {
        uint32_t index = (hashedIndex + i) % capacity;
        data[index].l.lock();
        int found = data[index].key;

        if (found == NULL_VAL)
        {
            data[index].l.unlock();
            return false;
        }
        else if (found == key)
        {
            data[index].key = TOMBSTONE;
            data[index].l.unlock();
            return true;
        }
        data[index].l.unlock();
    }

    return false;
}

// semantics: return the sum of all KEYS in the set
int64_t AlgorithmA::getSumOfKeys()
{
    // because this function is called at the end of threads' work.
    // I have not guard it with a lock.
    int64_t keySummation = 0;
    for (int i = 0; i < capacity; i++)
        keySummation += ((data[i].key == NULL_VAL || data[i].key == TOMBSTONE) ? 0 : data[i].key);

    return keySummation;
}

// print any debugging details you want at the end of a trial in this function
void AlgorithmA::printDebuggingDetails()
{
}
