#/bin/bash


algorithms=('A' 'B' 'C' 'D')
tableSize=100
timeToRun=10000
numberOfThreads=16
initialCmd="LD_PRELOAD=./libjemalloc.so taskset -c 0-15 perf stat  -a -e  LLC-load-loads,LLC-load-misses,L1-dcache-loads,L1-dcache-load-misses,L1-dcache-stores ./benchmark.out"

for algorithm in ${algorithms[@]}
do
    cmd="$initialCmd -a $algorithm -sT $tableSize -m $timeToRun -t $numberOfThreads"
    echo $cmd
done

