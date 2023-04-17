import re
import os
import matplotlib.pyplot as plt

def parse_output(filename='temp.txt'):
    with open(os.path.join(os.getcwd(), filename), 'r') as file:
        lines = file.readlines()
        for line in lines:
            found = re.search('\s+([\d+,]+)\s+LLC', line)
            if found is not None and len(found.groups()) != 0:
                num = found.groups()[0]
                num = re.sub(',', '', num)
                cache_miss = float(num)
                return cache_miss


def parse_throuput(output):
    found = re.search("throughput\s+:\s+(\d+)", output)
    if found is not None and len(found.groups()) != 0:
        num = float(found.groups()[0])
        return num
    raise Exception()

cmd = "LD_PRELOAD=./libjemalloc.so taskset -c 0-15 perf stat -o temp.txt -a -e  LLC-load-misses ./benchmark.out -a {} -sT 1000 -sR 1000000 -m 5000 -t {}"

def test_threads():
    os.system("make all -j8")
    threads = [1, 4, 8, 16]
    cache_misses = []
    throughputs  = []

    for thread in threads:
        th1 = []
        th2 = []
        cm_1 = []
        cm_2 = []
        for _ in range(5):
            output1 = os.popen(cmd.format("A", thread)).read()
            throughput_v1 = parse_throuput(output1)
            cache_miss_v1 = parse_output()
            
            output2 = os.popen(cmd.format("B", thread)).read()
            throughput_v2 = parse_throuput(output2)
            cache_miss_v2 = parse_output()
            
            cm_1.append(cache_miss_v1)
            cm_2.append(cache_miss_v2)
            th1.append(throughput_v1)
            th2.append(throughput_v2)

        throughputs.append([sum(th1)/len(th1), sum(th2)/len(th2)])
        cache_misses.append([sum(cm_1)/len(cm_1), sum(cm_2)/len(cm_2)])
    
    print(throughputs, cache_misses)
    position_graber = lambda index: lambda x: x[index]

    plt.figure(figsize=(10, 7))
    plt.subplot(2, 1, 1)
    plt.plot(threads, list(map(position_graber(0), throughputs)), label="A-algorithm", marker='o')
    plt.plot(threads, list(map(position_graber(-1), throughputs)), label="B-algorithm", marker='o')
    plt.grid(True)
    plt.legend()
    plt.ylabel("throughput")
    plt.xlabel("threads_count")

    plt.subplot(2, 1, 2)
    plt.plot(threads, list(map(position_graber(0), cache_misses)), label="A-algorithm", marker='o')
    plt.plot(threads, list(map(position_graber(-1), cache_misses)), label="B-algorithm", marker='o') 
    plt.grid(True)
    plt.legend()
    plt.ylabel("LLC_MISSES_RATE")
    plt.xlabel("threads_count")

    plt.savefig("A-B-cache-misses.png")
        
    

test_threads()