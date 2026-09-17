#!/bin/bash


# 测试次数
NUM_RUNS=10


# 保存目录
RESULT_DIR="results_report"


mkdir -p $RESULT_DIR


echo "Start benchmark: $NUM_RUNS runs"


for i in $(seq -w 1 $NUM_RUNS)
do

    echo "=============================="
    echo "Running test $i"
    echo "=============================="


    # 当前测试目录
    TEST_DIR="${RESULT_DIR}/test_${i}"


    mkdir -p $TEST_DIR


    # tegrastats日志
    TEGRA_LOG="${TEST_DIR}/tegra_log.txt"


    # 启动监控
    tegrastats \
        --interval 1000 \
        --logfile $TEGRA_LOG &


    PID=$!


    # 运行算法
    python bleed_detection.py


    # 停止tegrastats
    kill $PID


    # 保存性能报告
    mv performance_report.json \
       ${TEST_DIR}/performance_report.json


    echo "Test $i finished"


    sleep 2

done


echo "All tests finished."
