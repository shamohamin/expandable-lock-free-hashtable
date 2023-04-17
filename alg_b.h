#pragma once
#include "util.h"
#include <atomic>
#include <mutex>
using namespace std;

class AlgorithmB
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
        volatile int key;
        std::mutex _lock;
        // pthread_spinlock_t _lock;
        char padding[PADDING_BYTES - sizeof(key) - sizeof(_lock)];
        PaddedIntLocked()
        {
            key = NULL_VAL;
            // pthread_spin_init(&_lock, PTHREAD_PROCESS_PRIVATE);
        }

        void lockL() {
            _lock.lock();
            // pthread_spin_lock(&data[index]._lock);
        }

        void unLock() {
            _lock.unlock();
            // pthread_spin_unlock(&data[index]._lock);
        }
    };

private:
    PaddedIntLocked *data;

public:
    AlgorithmB(const int _numThreads, const int _capacity);
    ~AlgorithmB();
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
AlgorithmB::AlgorithmB(const int _numThreads, const int _capacity)
    : numThreads(_numThreads), capacity(_capacity)
{
    data = new PaddedIntLocked[capacity];
    for (int i = 0; i < capacity; i++)
    {
        data[i].key = NULL_VAL;
    }
        
}

// destructor: clean up any allocated memory, etc.
AlgorithmB::~AlgorithmB()
{
    delete[] data;
}

// semantics: try to insert key. return true if successful (if key doesn't already exist), and false otherwise
bool AlgorithmB::insertIfAbsent(const int tid, const int &key)
{
    uint32_t hashedIndex = murmur3(key);
    for (int i = 0; i < capacity; i++)
    {
        uint32_t index = (hashedIndex + i) % (uint32_t)capacity;
        int found = data[index].key;
        if (found == NULL_VAL)
        {
            data[index].lockL();
            // pthread_spin_lock(&data[index]._lock);
            found = data[index].key;
            if (found == NULL_VAL)
            {
                data[index].key = key;
                data[index].unLock();
                // pthread_spin_unlock(&data[index]._lock);
                return true;
            }
            else if (found == key)
            {
                data[index].unLock();
                // pthread_spin_unlock(&data[index]._lock);
                return false;
            }
            data[index].unLock();
        }
        else if (found == key)
        {
            return false;
        }
    }

    return false;
}

// semantics: try to erase key. return true if successful, and false otherwise
bool AlgorithmB::erase(const int tid, const int &key)
{
    uint32_t hashedIndex = murmur3(key);
    for (int i = 0; i < capacity; ++i)
    {
        uint32_t index = (hashedIndex + i) % capacity;

        int found = data[index].key;
        if (found == NULL_VAL)
        {
            return false;
        }
        else if (found == key)
        {
            data[index].lockL();
            found = data[index].key;
            if (found == key)
            {
                data[index].key = TOMBSTONE;
                data[index].unLock();
                // pthread_spin_unlock(&data[index]._lock);
                return true;
            }else if(found == NULL_VAL) {
                data[index].unLock();
                // pthread_spin_unlock(&data[index]._lock);
                return false;
            }
            data[index].unLock();
            // pthread_spin_unlock(&data[index]._lock);
        }
    }

    return false;
}

// semantics: return the sum of all KEYS in the set
int64_t AlgorithmB::getSumOfKeys()
{
    // because this function is called at the end of threads' work.
    // I have not guard it with a lock.
    int64_t keySummation = 0;
    for (int i = 0; i < capacity; i++)
        keySummation += ((data[i].key == NULL_VAL || data[i].key == TOMBSTONE) ? 0 : data[i].key);

    return keySummation;
}

// print any debugging details you want at the end of a trial in this function
void AlgorithmB::printDebuggingDetails()
{
}
