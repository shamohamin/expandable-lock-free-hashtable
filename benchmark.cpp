/**
 * A simple insert & delete benchmark for (unordered) sets (e.g., hash tables).
 */

#include <thread>
#include <cstdlib>
#include <atomic>
#include <string>
#include <cstring>
#include <iostream>
#include <time.h>

#include "util.h"
#include "alg_a.h"
#include "alg_b.h"
#include "alg_c.h"
#include "alg_d.h"
#include "alg_aa.h"

using namespace std;

template <class DataStructureType>
struct globals_t {
    PaddedRandom rngs[MAX_THREADS];
    volatile char padding0[PADDING_BYTES];
    ElapsedTimer timer;
    volatile char padding1[PADDING_BYTES];
    long elapsedMillis;
    volatile char padding2[PADDING_BYTES];
    volatile bool done;
    volatile char padding3[PADDING_BYTES];
    volatile bool start;        // used for a custom barrier implementation (should threads start yet?)
    volatile char padding4[PADDING_BYTES];
    atomic_int running;         // used for a custom barrier implementation (how many threads are waiting?)
    volatile char padding5[PADDING_BYTES];
    DataStructureType * ds;
    debugCounter numTotalOps;   // already has padding built in at the beginning and end
    debugCounter keyChecksum;
    int millisToRun;
    int totalThreads;
    int keyRangeSize;
    int tableSize;
    volatile char padding7[PADDING_BYTES];
    
    globals_t(int _millisToRun, int _totalThreads, int _keyRangeSize, int _tableSize, DataStructureType * _ds) {
        for (int i=0;i<MAX_THREADS;++i) {
            rngs[i].setSeed(i+1); // +1 because we don't want thread 0 to get a seed of 0, since seeds of 0 usually mean all random numbers are zero...
        }
        elapsedMillis = 0;
        done = false;
        start = false;
        running = 0;
        ds = _ds;
        millisToRun = _millisToRun;
        totalThreads = _totalThreads;
        keyRangeSize = _keyRangeSize;
        tableSize = _tableSize;
    }
    ~globals_t() {
        delete ds;
    }
} __attribute__((aligned(PADDING_BYTES)));

void printUpdatedThroughput(auto g, int64_t elapsedNow) {
    auto opsNow = g->numTotalOps.getTotal();
    cout<<elapsedNow <<"ms: "<<opsNow<<" total_ops"<<endl;
    cout<<elapsedNow <<"ms: "<<(opsNow * 1000 / elapsedNow)<<" throughput"<<endl;
}

template <class DataStructureType>
void runExperiment(int keyRangeSize, int tableSize, int millisToRun, int totalThreads) {
    // create globals struct that all threads will access (with padding to prevent false sharing on control logic meta data)
    auto dataStructure = new DataStructureType(totalThreads, tableSize);
    auto g = new globals_t<DataStructureType>(millisToRun, totalThreads, keyRangeSize, tableSize, dataStructure);
    
    /**
     * 
     * RUN EXPERIMENT
     * 
     */
    
    // create and start threads
    thread * threads[MAX_THREADS]; // just allocate an array for max threads to avoid changing data layout (which can affect results) when varying thread count. the small amount of wasted space is not a big deal.
    for (int tid=0;tid<g->totalThreads;++tid) {
        threads[tid] = new thread([&, tid]() { /* access all variables by reference, except tid, which we copy (since we don't want our tid to be a reference to the changing loop variable) */
                const int OPS_BETWEEN_TIME_CHECKS = 500; // only check the current time (to see if we should stop) once every X operations, to amortize the overhead of time checking

                // BARRIER WAIT
                g->running.fetch_add(1);
                while (!g->start) { TRACE TPRINT("waiting to start"); } // wait to start
                
                for (int cnt=0; !g->done; ++cnt) {
                    if ((cnt % OPS_BETWEEN_TIME_CHECKS) == 0                    // once every X operations
                        && g->timer.getElapsedMillis() >= g->millisToRun) {   // check how much time has passed
                            g->done = true; // set global "done" bit flag, so all threads know to stop on the next operation (first guy to stop dictates when everyone else stops --- at most one more operation is performed per thread!)
                            __sync_synchronize(); // flush the write to g->done so other threads see it immediately (mostly paranoia, since volatile writes should be flushed, and also our next step will be a fetch&add which is an implied flush on intel/amd)
                    }

                    VERBOSE if (cnt&&((cnt % 1000000) == 0)) TPRINT("op# "<<cnt);
                    
                    // flip a coin to decide: insert or erase?
                    // generate a random double in [0, 1]
                    double operationType = g->rngs[tid].nextNatural() / (double) numeric_limits<unsigned int>::max();
                    //cout<<"operationType="<<operationType<<endl;
                    
                    // generate random key
                    int key = 1 + (g->rngs[tid].nextNatural() % g->keyRangeSize);
                    
                    // insert or delete this key
                    if (operationType < 0.5) {
                        auto result = g->ds->insertIfAbsent(tid, key);
                        if (result) g->keyChecksum.add(tid, key);
                    } else {
                        auto result = g->ds->erase(tid, key);
                        if (result) g->keyChecksum.add(tid, -key);
                    }

                    g->numTotalOps.inc(tid);
                }
                
                g->running.fetch_add(-1);
                TPRINT("terminated");
        });
    }

    while (g->running < g->totalThreads) {
        TRACE printf("main thread: waiting for threads to START running=%d\n", g->running.load());
    } // wait for all threads to be ready
    
    printf("main thread: starting timer...\n");
    g->timer.startTimer();
    __asm__ __volatile__ ("" ::: "memory"); // prevent compiler from reordering "start = true;" before the timer start; this is mostly paranoia, since start is volatile, and nothing should be reordered around volatile reads/writes (by the *compiler*)
    
    g->start = true; // release all threads from the barrier, so they can work
    __sync_synchronize(); // flush store buffer so other threads can see the write to g->start immediately (so they start working ASAP)
    
    
    // wait for all threads to stop working,
    // and print throughput update every 1s
    
    int64_t lastTime = 0;
    while (g->running > 0) {
        // sleep for 0.1s
        timespec time_to_sleep;
        time_to_sleep.tv_sec = 0;
        time_to_sleep.tv_nsec = 100000000;
        nanosleep(&time_to_sleep, NULL);
        
        // check if the most recent 0.1s sleep pushed us over a new 1s mark
        auto elapsedNow = g->timer.getElapsedMillis();
        if (elapsedNow % 1000 < 100) {
            printUpdatedThroughput(g, elapsedNow);
        }
        lastTime = elapsedNow;
    }
    
    // measure and print elapsed time
    g->elapsedMillis = g->timer.getElapsedMillis();
    cout<<(g->elapsedMillis/1000.)<<"s"<<endl;
    
    if (g->elapsedMillis - lastTime > 100 && (g->elapsedMillis % 1000) < 100) {
        printUpdatedThroughput(g, g->elapsedMillis);
    }
    
    // join all threads
    for (int tid=0;tid<g->totalThreads;++tid) {
        threads[tid]->join();
        delete threads[tid];
    }
    
    /**
     * 
     * PRODUCE OUTPUT
     * 
     * 
     */
    
    g->ds->printDebuggingDetails();
    
    auto numTotalOps = g->numTotalOps.getTotal();
    auto dsSumOfKeys = g->ds->getSumOfKeys();
    auto threadsSumOfKeys = g->keyChecksum.getTotal();
    cout<<"Validation: sum of keys according to the data structure = "<<dsSumOfKeys<<" and sum of keys according to the threads = "<<threadsSumOfKeys<<".";
    cout<<((threadsSumOfKeys == dsSumOfKeys) ? " OK." : " FAILED.")<<endl;
    cout<<endl;

    if (threadsSumOfKeys != dsSumOfKeys) {
        cout<<"ERROR: validation failed!"<<endl;
        exit(-1);
    }
    
    cout<<"individual thread ops :";
    for (int i=0;i<g->totalThreads;++i) {
        cout<<" "<<g->numTotalOps.get(i);
    }
    cout<<endl;
    cout<<"total completed ops   : "<<numTotalOps<<endl;
    cout<<"throughput            : "<<(long long) (numTotalOps * 1000. / g->elapsedMillis)<<endl;
    cout<<"elapsed milliseconds  : "<<g->elapsedMillis<<endl;
    cout<<endl;
    
    delete g;
}

int main(int argc, char** argv) {
    if (argc == 1) {
        cout<<"USAGE: "<<argv[0]<<" [options]"<<endl;
        cout<<"Options:"<<endl;
        cout<<"    -a  [string]   [a]lgorithm name in { A, B, C, D }"<<endl;
        cout<<"    -sT [int]      size of initial hash [T]able"<<endl;
        cout<<"    -m  [int]      [m]illiseconds to run"<<endl;
        cout<<"    -sR [int]      size of the key [R]ange that random keys will be drawn from (i.e., range [1, s])"<<endl;
        cout<<"    -t  [int]      number of [t]hreads that will perform inserts and deletes"<<endl;
        cout<<endl;
        cout<<"Example: "<<argv[0]<<" -a D -m 10000 -sT 1000 -sR 1000000 -t 16"<<endl;
        return 1;
    }
    
    int millisToRun = -1;
    int tableSize = 0;
    int keyRangeSize = 0;
    int totalThreads = 0;
    char * alg = NULL;
    
    //read command line args
    for (int i=1;i<argc;++i) {
        if (strcmp(argv[i], "-sT") == 0) {
            tableSize = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-sR") == 0) {
            keyRangeSize = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0) {
            totalThreads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0) {
            millisToRun = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-a") == 0) {
            alg = argv[++i];
        } else {
            cout<<"bad arguments"<<endl;
            exit(1);
        }
    }
    
    // print command and args for debugging
    std::cout<<"Cmd:";
    for (int i=0;i<argc;++i) {
        std::cout<<" "<<argv[i];
    }
    std::cout<<std::endl;
    
    // print configuration for debugging
    PRINT(MAX_THREADS);
    PRINT(millisToRun);
    PRINT(keyRangeSize);
    PRINT(tableSize);
    PRINT(totalThreads);
    PRINT(alg);
    cout<<endl;
    
    // check for too large thread count
    if (totalThreads >= MAX_THREADS) {
        std::cout<<"ERROR: totalThreads="<<totalThreads<<" >= MAX_THREADS="<<MAX_THREADS<<std::endl;
        return 1;
    }
    
    // check for missing alg name
    if (alg == NULL) {
        cout<<"Must specify algorithm name"<<endl;
        return 1;
    }
    
    // run experiment for the selected algorithm
    if (!strcmp(alg, "A")) {
        runExperiment<AlgorithmA>(keyRangeSize, tableSize, millisToRun, totalThreads);
    }
	else if (!strcmp(alg, "B")) {
         runExperiment<AlgorithmB>(keyRangeSize, tableSize, millisToRun, totalThreads);
    }
	else if (!strcmp(alg, "C")) {
         runExperiment<AlgorithmC>(keyRangeSize, tableSize, millisToRun, totalThreads);
    }
	else if (!strcmp(alg, "D")) {
         runExperiment<AlgorithmD>(keyRangeSize, tableSize, millisToRun, totalThreads);
    } 
    else if (!strcmp(alg, "AA")) {
        runExperiment<AlgorithmAA>(keyRangeSize, tableSize, millisToRun, totalThreads); 
    }
 	else {
        cout<<"Bad algorithm name: "<<alg<<endl;
        return 1;
    }
    
    return 0;
}
