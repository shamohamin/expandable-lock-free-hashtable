import matplotlib.pyplot as plt
import os
# def parse()


command = "LD_PRELOAD=./libjemalloc.so taskset -c 0-15 ./benchmark.out -a {} -sT {} -sR 10000000 -m 50000 -t 16"

def expriment():
    tableSizes = [1000, 10000, 100000, 1000000]
    times = list(range(0, 50000, 1000))
    algorithms = ['B', 'C']
    results = dict.fromkeys(algorithms)
    for algorithm in algorithms:
        results[algorithm] = dict.fromkeys(tableSizes)

    for tableSize in tableSizes:
        for algorithm in algorithms:
            cmd = command.format(algorithm, tableSize)
            cmd += " | awk  -F\' \' \'{if($3 == \"throughput\" ) print $2}\' "
            output = os.popen(cmd).read()            
            throughputs = list(map(float, output.split('\n')[:-1]))
            print(algorithm, throughputs)

            results[algorithm][tableSize] = throughputs[:50]

    fig, axes = plt.subplots(2, 2, figsize=(12, 7))
    axes = axes.ravel()
    fig.tight_layout(pad=2.5)
    print(results)
    for algo in algorithms:
        algo_res = results[algo]
        for index,tableSize in enumerate(tableSizes):
            ax = axes[index]
            ax.plot(times, algo_res[tableSize], label=algo)
   
    for index, ax in enumerate(axes):
        ax.set_title(f'table_size({tableSizes[index]})')
        ax.set_ylabel("throughput")
        ax.set_xlabel("time(milliseconds)")
        ax.grid(True)
        ax.legend()
    
    plt.savefig("B-C.png")
        
    print(results)

expriment()
            