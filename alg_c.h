#pragma once
#include "util.h"
#include <atomic>
using namespace std;

class AlgorithmC
{
public:
    static constexpr int TOMBSTONE = -1;
    static constexpr int NULL_VAL = -2;

    char padding0[PADDING_BYTES];
    const int numThreads;
    int capacity;
    char padding2[PADDING_BYTES];

    struct PaddedAtomic
    {
        atomic<int> key;
        char padding[PADDING_BYTES - sizeof(key)];
    };

private:
    PaddedAtomic *data;

public:
    AlgorithmC(const int _numThreads, const int _capacity);
    ~AlgorithmC();
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
AlgorithmC::AlgorithmC(const int _numThreads, const int _capacity)
    : numThreads(_numThreads), capacity(_capacity)
{
    data = new PaddedAtomic[capacity];
    for (int i = 0; i < capacity; i++)
        data[i].key = NULL_VAL;
}

// destructor: clean up any allocated memory, etc.
AlgorithmC::~AlgorithmC()
{
    delete[] data;
}

// semantics: try to insert key. return true if successful (if key doesn't already exist), and false otherwise
bool AlgorithmC::insertIfAbsent(const int tid, const int &key)
{
    uint32_t hashedIndex = murmur3(key);
    for (int i = 0; i < capacity; ++i)
    {
        uint32_t index = (hashedIndex + i) % capacity;
        int found = data[index].key.load(memory_order_relaxed);
        if (found == NULL_VAL)
        {
            int EXPECTED = NULL_VAL;
            if (data[index].key.compare_exchange_strong(EXPECTED, key, memory_order_relaxed)) // seq point
                return true;
            else if (data[index].key.load() == key)
                return false;
        }
        else if (found == key)
            return false;
    }
    return false;
}

// semantics: try to erase key. return true if successful, and false otherwise
bool AlgorithmC::erase(const int tid, const int &key)
{
    uint32_t hashedIndex = murmur3(key);
    int DESIRED = TOMBSTONE;
    int k = key;

    for (int i = 0; i < capacity; ++i)
    {
        uint32_t index = (hashedIndex + i) % capacity;
        int found = data[index].key.load(memory_order_relaxed);
        if (found == NULL_VAL)
            return false;
        else if (found == key)
            return data[index].key.compare_exchange_strong(k, TOMBSTONE, memory_order_relaxed); // sequential point
    }

    return false;
}

// semantics: return the sum of all KEYS in the set
int64_t AlgorithmC::getSumOfKeys()
{
    int64_t keySummation = 0;
    for (int i = 0; i < capacity; i++)
        keySummation += ((data[i].key == NULL_VAL || data[i].key == TOMBSTONE) ? 0 : data[i].key.load());

    return keySummation;
}

// print any debugging details you want at the end of a trial in this function
void AlgorithmC::printDebuggingDetails()
{
}
