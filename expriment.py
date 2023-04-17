import matplotlib.pyplot as plt
import os

def exprimentDifferentLocks():
    command = "LD_PRELOAD=./libjemalloc.so ./benchmark.out -a AA -sT 10000 -sR 10000 -m 10000 -t {}"
    
    threads = [2, 4, 8, 16, 24, 32]
    lockTypes = ['HYBRID_SPIN_LOCK', 'MUTEX', 'SPIN_LOCK', 'HYBRID_MUTEX', 'HYBRID_FUTEX']
    results = dict.fromkeys(lockTypes)
    
    for lockType in lockTypes:
        results[lockType] = []
        cmd = f"make USER_DEFINES=\"-D{lockType}\" -j8"
        os.popen(cmd)
        for threadCount in threads:
            cmd = command.format(threadCount)
            cmd += " | awk  -F: \'{if($1 == \"total completed ops   \" ) print $2}\' "
            output = os.popen(cmd).read()
            print(output)
            # throughputs = list(map(float, output.split('\n')[:-1]))
            # results[lockType].append(sum(throughputs) / len(throughputs))
            results[lockType].append(float(str(output).strip()))
            print(f"DONE FOR {lockType}")
    
    print(results)
    plt.figure(figsize=(12, 7))

    for lockType in lockTypes:
        lockRes = results[lockType]
        plt.plot(threads, lockRes, label=lockType, marker='o')
    
    plt.title("Hashmap Performance")
    plt.xlabel("Number of Threads")
    plt.ylabel("Number of operations after 10s")
    plt.xticks(threads)
    plt.title("hashmap")
    plt.grid(True)
    plt.legend()
    plt.savefig('locks.png')


def exprimentTableSizes():
    command = "LD_PRELOAD=./libjemalloc.so taskset -c 0-15 ./benchmark.out -a {} -sT {} -sR 10000000 -m 10000 -t 16"
    tableSizes = [1000, 10000, 100000, 1000000]
    times = list(range(0, 10000, 1000))
    algorithms = ['A', 'B', 'C', 'D']

    expriment(algorithms, times, tableSizes, "tableSize-small-Range", command)

def exprimentkeyRange():
    times = 10000
    command = "LD_PRELOAD=./libjemalloc.so taskset -c 0-15 ./benchmark.out -a {} -sT 1000000 -sR {} -m " + str(times) + " -t 16"
    keyRanges = [1000, 10000, 100000, 1000000]
    times = list(range(0, times, 1000))
    algorithms = ['A', 'B', 'C', 'D']

    expriment(algorithms, times, keyRanges, "keyRanges", command) 


def expriment(algorithms: list, x: list, toBeCalculated: list, baseTitle: str, command: str):
    results = dict.fromkeys(algorithms)
    for algorithm in algorithms:
        results[algorithm] = dict.fromkeys(toBeCalculated)

    for calC in toBeCalculated:
        for algorithm in algorithms:
            cmd = command.format(algorithm, calC)
            cmd += " | awk  -F\' \' \'{if($3 == \"throughput\" ) print $2}\' "
            output = os.popen(cmd).read()            
            throughputs = list(map(float, output.split('\n')[:-1]))
            results[algorithm][calC] = throughputs[:len(x)]
            print(algorithm, "FOR ", calC, "Is Done")

    fig, axes = plt.subplots(2, 2, figsize=(12, 7))
    axes = axes.ravel()
    fig.tight_layout(pad=2.5)
    print(results)

    for algo in algorithms:
        algo_res = results[algo]
        for index, calC in enumerate(toBeCalculated):
            ax = axes[index]
            ax.plot(x, algo_res[calC], label=algo)
   
    for index, ax in enumerate(axes):
        ax.set_title(f'{baseTitle}({toBeCalculated[index]})')
        ax.set_ylabel("throughput")
        ax.set_xlabel("time(milliseconds)")
        ax.grid(True)
        ax.legend()
    
    plt.savefig(f"A-B-C-D_{baseTitle}.png")

    return results

# exprimentTableSizes()
# exprimentkeyRange() 
exprimentDifferentLocks()