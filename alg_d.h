#pragma once
#include "util.h"
#include <atomic>
#include <math.h>
#include <cassert>
#include <iostream>
#include <stdlib.h>
using namespace std;

#define _CAS(val, _expected, _desired) \
    __atomic_compare_exchange_n(&val, (void *)&_expected, _desired, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)
#define _CAS_RELAXED(val, _expected, _desired) \
    __atomic_compare_exchange_n(&val, (void *)&_expected, _desired, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)
#define READ_ATOMIC_RELAXED(val) __atomic_load_n(&val, __ATOMIC_RELAXED)
#define READ_ATOMIC(val) __atomic_load_n(&val, __ATOMIC_SEQ_CST)

#define CHUNK_SIZE 4096
#define DEFAULT_SIZE_EXPANSION 4
#define MAX_PROBING_SIZE 100

class AlgorithmD
{
private:
    enum
    {
        MARKED_MASK = (int)0x80000000, // most significant bit of a 32-bit key
        TOMBSTONE = (int)0x7FFFFFFF,   // largest value that doesn't use bit MARKED_MASK
        EMPTY = (int)0,
        MAXIMUM_HASH = (uint32_t)0xFFFFFFFF
    }; // with these definitions, the largest "real" key we allow in the table is 0x7FFFFFFE, and the smallest is 1 !!

    struct table
    {
        // data types
        char padding0[PADDING_BYTES];
        volatile int *data;
        volatile int *oldData;
        counter *approxCounter;
        counter *deleteCounter;
        int capacity, oldCapacity, numThreads;
        char padding1[PADDING_BYTES];
        atomic<int> chunksClaimed;
        char padding2[PADDING_BYTES - sizeof(chunksClaimed)];
        atomic<int> chunksDone;
        char padding3[PADDING_BYTES - sizeof(chunksDone)];
        // constructors
        table(int size, int _numThreads)
        {
            capacity = size;
            oldCapacity = 0;
            oldData = NULL;
            numThreads = _numThreads;
            approxCounter = new counter(_numThreads);
            deleteCounter = new counter(_numThreads);
            data = (volatile int *)malloc(sizeof(volatile int) * size);
            if (data)
                initilizing_the_arr(size);

            atomic_init(&chunksClaimed, 0);
            atomic_init(&chunksDone, 0);
        }

        table(table *oldTable, int const tid)
        {
            oldCapacity = oldTable->capacity;
            oldData = oldTable->data; // pointing to the old data.
            numThreads = oldTable->numThreads;

            int insertCount = oldTable->approxCounter->get();
            int deleteCount = oldTable->deleteCounter->get();
            int temp = insertCount - deleteCount; // numberOfKeys

            approxCounter = new counter(numThreads);
            deleteCounter = new counter(numThreads);

            if (temp > 0)
                capacity = temp * DEFAULT_SIZE_EXPANSION;
            else
                capacity = oldCapacity * DEFAULT_SIZE_EXPANSION;
        

            data = (volatile int *)malloc(sizeof(volatile int) * capacity);
            if (data)
                initilizing_the_arr(capacity);

            atomic_init(&chunksClaimed, 0);
            atomic_init(&chunksDone, 0);
        }
        // destructor
        ~table()
        {
            if (data)
                free((void *)data);
            if (approxCounter)
                delete approxCounter;
            if (deleteCounter)
                delete deleteCounter;
        }

        inline const int calculatingTotalChunks() const
        {
            return ceil(oldCapacity / (double)CHUNK_SIZE);
        }

        void fancyPrint()
        {
            for (int i = 0; i < capacity; i++)
            {
                int temp = data[i];
                if (temp == TOMBSTONE)
                    cout << "O";
                else if (temp == EMPTY)
                    cout << ".";
                else
                    cout << 'X';
            }
            cout << "END\n *** \n *** \n";
        }

    private:
        table &operator=(const table &) = delete; // no assignment;
        void initilizing_the_arr(int size)
        {
            for (int i = 0; i < size; i++)
                __atomic_store_n(&data[i], EMPTY, __ATOMIC_RELAXED);
            __sync_synchronize();
        }
    };

    bool expandAsNeeded(const int tid, table *t, int i);
    void helpExpansion(const int tid, table *t);
    void startExpansion(const int tid, table *t);
    void migrate(const int tid, table *t, int myChunk);

    char padding0[PADDING_BYTES];
    int numThreads;
    int initCapacity;
    // more fields (pad as appropriate)
    atomic<table *> currTable;

    char padding1[PADDING_BYTES];

    inline void markOldDataEntries(table *t, int &lowerBound, int &higherBound);
    inline bool insertHelper(table *t, const int tid, int key, bool safe);
    inline void waitOnExpansion(table *t, int totalChunks);

public:
    AlgorithmD(const int _numThreads, const int _capacity);
    ~AlgorithmD();
    bool insertIfAbsent(const int tid, const int &key, bool disableExpansion);
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
AlgorithmD::AlgorithmD(const int _numThreads, const int _capacity)
    : numThreads(_numThreads), initCapacity(_capacity)
{
    currTable = new table(_capacity, numThreads);
}

// destructor: clean up any allocated memory, etc.
AlgorithmD::~AlgorithmD()
{
    table *t = currTable.load();
    if (t)
    {
        if (t->oldData)
            delete[] currTable.load()->oldData;
        if (t->approxCounter)
            delete currTable.load()->approxCounter;
        delete t; // call Destructor
    }
}

bool AlgorithmD::expandAsNeeded(const int tid, table *t, int i)
{
    helpExpansion(tid, t);
    if (
        (t->approxCounter->get() > (t->capacity / 2)) ||
        ((i > MAX_PROBING_SIZE) && (t->approxCounter->getAccurate() > (int)(t->capacity / 2))))
    {
        startExpansion(tid, t);
        return true;
    }

    return false;
}

void AlgorithmD::helpExpansion(const int tid, table *t)
{
    int totalChunks = t->calculatingTotalChunks();
    while (t->chunksClaimed.load(memory_order_relaxed) < totalChunks)
    {
        int myChunk = t->chunksClaimed.fetch_add(1, memory_order_relaxed);
        if (myChunk < totalChunks)
        {
            migrate(tid, t, myChunk);
            t->chunksDone.fetch_add(1, memory_order_relaxed);
        }
    }

    waitOnExpansion(t, totalChunks);
    // the table expansion is over
}

inline void AlgorithmD::waitOnExpansion(table *t, int totalChunks)
{
    while (t->chunksDone.load(memory_order_relaxed) < totalChunks)
    {}
}

void AlgorithmD::startExpansion(const int tid, table *t)
{
    if (currTable == t)
    {

        table *newTable = new table(t, tid);

        if (!currTable.compare_exchange_strong(t, newTable))
            delete newTable;
        else
        {
            if (t->oldData)
                free((void *)t->oldData);
        }
    }
    helpExpansion(tid, currTable);
}

void AlgorithmD::migrate(const int tid, table *t, int myChunk)
{
    int lowerBound = myChunk * CHUNK_SIZE;
    int higherBound = min((myChunk + 1) * CHUNK_SIZE, t->oldCapacity);
    markOldDataEntries(t, lowerBound, higherBound); // marking old data entries

    int subLowerBound = lowerBound - 1;
    int incUpperBound = higherBound + 1;
    bool safeToCopy = ((subLowerBound < 0) ? true : READ_ATOMIC(t->oldData[subLowerBound]) == EMPTY) &&
                      ((incUpperBound >= t->oldCapacity) ? true : READ_ATOMIC(t->oldData[incUpperBound]) == EMPTY);

    for (int i = lowerBound; i < higherBound; i++)
    {
        // int unmaskedData = (t->oldData[i].load(memory_order_relaxed) & ~(MARKED_MASK));
        int unmaskedData = READ_ATOMIC(t->oldData[i]) & ~(MARKED_MASK);
        if (unmaskedData != EMPTY && unmaskedData != TOMBSTONE)
            insertHelper(t, tid, unmaskedData, safeToCopy); // unmarking the data.
    }
    __sync_synchronize();
}

inline bool AlgorithmD::insertHelper(table *t, const int tid, int key, bool safe)
{
    double ii = murmur3(key);
    uint32_t hashedIndex = floor(ii / MAXIMUM_HASH * (uint32_t)t->capacity);

    for (uint32_t j = 0; j < t->capacity; ++j)
    {
        uint32_t index = (hashedIndex + j) % (uint32_t)t->capacity;

        if (safe)
        {
            int found = t->data[index];
            if (found == EMPTY)
            {
                t->data[index] = key;
                t->approxCounter->inc(tid);
                return true;
            }
            else if (found == key)
                return false;
        }
        else
        {
            int found = READ_ATOMIC_RELAXED(t->data[index]);
            if (found == EMPTY)
            {
                if (_CAS_RELAXED(t->data[index], found, key))
                {
                    t->approxCounter->inc(tid);
                    return true;
                }
            }
            else if (found == key)
                return false;
        }
    }
    return false;
}

inline void AlgorithmD::markOldDataEntries(table *t, int &lowerBound, int &higherBound)
{
    for (int i = lowerBound; i < higherBound; i++)
    {
        do
        {
            int unmarkedData = READ_ATOMIC(t->oldData[i]);
            if (_CAS(t->oldData[i], unmarkedData, unmarkedData | MARKED_MASK)) // sync point
                break;

        } while (true); // try until you mark all the data.
    }
}

// semantics: try to insert key. return true if successful (if key doesn't already exist), and false otherwise
bool AlgorithmD::insertIfAbsent(const int tid, const int &key, bool disableExpansion = false)
{
    table *t = currTable.load();

    double ii = murmur3(key);
    uint32_t hashedIndex = floor(ii / MAXIMUM_HASH * (uint32_t)t->capacity);

    for (uint32_t i = 0; i < t->capacity; i++)
    {
        if (!disableExpansion)
            if (expandAsNeeded(tid, t, i))
                return insertIfAbsent(tid, key, disableExpansion);

        uint32_t index = (hashedIndex + i) % (uint32_t)t->capacity;

        int found = READ_ATOMIC(t->data[index]);

        if (found & MARKED_MASK)
            return insertIfAbsent(tid, key, disableExpansion);
        else if (found == key)
            return false;
        else if (found == EMPTY)
        {
            if (_CAS(t->data[index], found, key))
            {
                t->approxCounter->inc(tid);
                return true;
            }
            else
            {
                found = READ_ATOMIC(t->data[index]);
                if (found & MARKED_MASK)
                    return insertIfAbsent(tid, key, disableExpansion);
                else if (found == key)
                    return false;
            }
        }
    }

    return false;
}

// semantics: try to erase key. return true if successful, and false otherwise
bool AlgorithmD::erase(const int tid, const int &key)
{
    table *t = currTable.load();

    double ii = murmur3(key);
    uint32_t hashedIndex = floor(ii / MAXIMUM_HASH * (uint32_t)t->capacity);

    for (uint32_t i = 0; i < t->capacity; i++)
    {
        uint32_t index = (hashedIndex + i) % (uint32_t)t->capacity;
        helpExpansion(tid, t);

        int found = READ_ATOMIC(t->data[index]);

        if (found & MARKED_MASK)
            return erase(tid, key); // try until is on the new table.
        else if (found == EMPTY)
            return false;
        else if (found == key)
        {
            if (_CAS(t->data[index], found, TOMBSTONE))
            {
                t->deleteCounter->inc(tid);
                return true;
            }
            else
            { // failed

                found = READ_ATOMIC(t->data[index]);
                if (found & MARKED_MASK)    // maybe a expansion was going on.
                    return erase(tid, key); // try on new table.
                else if (found == TOMBSTONE)
                    return false;
            }
        }
    }
    return false;
}

// semantics: return the sum of all KEYS in the set
int64_t AlgorithmD::getSumOfKeys()
{
    table *t = currTable.load();
    int64_t summation = 0;

    for (int i = 0; i < t->capacity; i++)
    {
        int temp = READ_ATOMIC(t->data[i]);
        summation += ((temp == EMPTY || temp == TOMBSTONE) ? 0 : temp);
    }

    return summation;
}

// print any debugging details you want at the end of a trial in this function
void AlgorithmD::printDebuggingDetails()
{
}
