#!/usr/bin/env bash

set -euo pipefail

# Run from any directory. The input video can be passed as the first argument.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROGRAM="${SCRIPT_DIR}/build/bleed_detection"
INPUT_VIDEO="${1:-${SCRIPT_DIR}/b20241215_193214.mp4}"
RUNS="${RUNS:-10}"
RESULT_ROOT="${RESULT_ROOT:-${SCRIPT_DIR}/results_cpp_benchmark}"

if [[ ! -x "${PROGRAM}" || "${SCRIPT_DIR}/src/main.cpp" -nt "${PROGRAM}" ]]; then
    echo "Executable not found. Building the C++ program..."
    if command -v cmake >/dev/null 2>&1; then
        cmake -S "${SCRIPT_DIR}" -B "${SCRIPT_DIR}/build" -DCMAKE_BUILD_TYPE=Release
        cmake --build "${SCRIPT_DIR}/build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
    else
        CXX="${CXX:-g++}"
        if ! command -v "${CXX}" >/dev/null 2>&1; then
            echo "Error: g++ not found. Install a C++ compiler and OpenCV development files." >&2
            exit 1
        fi
        mkdir -p "${SCRIPT_DIR}/build"
        if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists opencv4; then
            # Linux distributions commonly provide OpenCV through pkg-config.
            "${CXX}" -std=c++17 -O3 -Wall -Wextra -Wpedantic \
                "$(pkg-config --cflags opencv4)" \
                "${SCRIPT_DIR}/src/main.cpp" \
                "$(pkg-config --libs opencv4)" \
                -o "${PROGRAM}"
        elif [[ -f /opt/anaconda3/include/opencv4/opencv2/core.hpp && -d /opt/anaconda3/lib ]]; then
            # Fallback for an OpenCV installation with headers and libraries in one prefix.
            "${CXX}" -std=c++17 -O3 -Wall -Wextra -Wpedantic \
                -I/opt/anaconda3/include/opencv4 "${SCRIPT_DIR}/src/main.cpp" \
                -L/opt/anaconda3/lib -lopencv_videoio -lopencv_imgproc -lopencv_core \
                -Wl,-rpath,/opt/anaconda3/lib -o "${PROGRAM}"
        else
            # Common Ubuntu/Jetson OpenCV layout.
            "${CXX}" -std=c++17 -O3 -Wall -Wextra -Wpedantic \
                -I/usr/include/opencv4 "${SCRIPT_DIR}/src/main.cpp" \
                -L/usr/lib/aarch64-linux-gnu -lopencv_videoio -lopencv_imgproc -lopencv_core \
                -o "${PROGRAM}"
        fi
    fi
fi

if [[ ! -f "${INPUT_VIDEO}" ]]; then
    echo "Error: input video not found: ${INPUT_VIDEO}" >&2
    echo "Usage: $0 /path/to/b20241215_193214.mp4" >&2
    exit 1
fi

if ! [[ "${RUNS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: RUNS must be a positive integer." >&2
    exit 1
fi

mkdir -p "${RESULT_ROOT}"
SUMMARY="${RESULT_ROOT}/summary.csv"
printf 'scale,run,time_sec,processing_fps,avg_frame_time_ms,detected_frames,empty_frames,detection_rate,cpu_avg_percent,gpu_avg_percent,ram_avg_mb\n' > "${SUMMARY}"

run_scale() {
    local scale="$1"
    local scale_name="$2"
    local scale_dir="${RESULT_ROOT}/${scale_name}"
    mkdir -p "${scale_dir}"

    echo "Starting scale=${scale} (${RUNS} runs)"
    for run in $(seq 1 "${RUNS}"); do
        local run_name
        run_name="$(printf 'test_%02d' "${run}")"
        local run_dir="${scale_dir}/${run_name}"
        local report="${run_dir}/performance_report.json"
        local tegra_log="${run_dir}/tegra_log.txt"
        local console_log="${run_dir}/console.log"
        mkdir -p "${run_dir}"

        echo "  [${run}/${RUNS}] scale=${scale}"
        if command -v tegrastats >/dev/null 2>&1; then
            # Redirect stdout directly because --logfile is not consistent across JetPack versions.
            tegrastats --interval 1000 > "${tegra_log}" 2>&1 &
            local tegra_pid=$!
        else
            : > "${tegra_log}"
            local tegra_pid=""
            echo "Warning: tegrastats not found; hardware log is empty." >&2
        fi

        "${PROGRAM}" \
            --scale "${scale}" \
            --input "${INPUT_VIDEO}" \
            --report "${report}" \
            --no-output \
            2>&1 | tee "${console_log}"

        if [[ -n "${tegra_pid}" ]] && kill -0 "${tegra_pid}" 2>/dev/null; then
            kill "${tegra_pid}" 2>/dev/null || true
            wait "${tegra_pid}" 2>/dev/null || true
        fi

        read -r cpu_avg gpu_avg ram_avg < <(awk '
            /CPU \[/ {
                line=$0; sub(/^.*CPU \[/, "", line); sub(/\].*$/, "", line)
                count=0; total=0
                n=split(line, cores, ",")
                for (i=1; i<=n; i++) if (match(cores[i], /[0-9]+%@/)) {
                    value=substr(cores[i], RSTART, RLENGTH-2); total+=value; count++
                }
                if (count>0) {cpu_sum+=total/count; cpu_count++}
            }
            /GR3D_FREQ [0-9]+%/ {line=$0; sub(/^.*GR3D_FREQ /, "", line); sub(/%.*/, "", line); gpu_sum+=line; gpu_count++}
            /RAM [0-9]+\// {line=$0; sub(/^.*RAM /, "", line); sub(/\/.*$/, "", line); ram_sum+=line; ram_count++}
            END {printf "%.2f %.2f %.2f\n", cpu_count?cpu_sum/cpu_count:0, gpu_count?gpu_sum/gpu_count:0, ram_count?ram_sum/ram_count:0}
        ' "${tegra_log}")

        awk -v scale="${scale}" -v run="${run}" -v cpu="${cpu_avg}" -v gpu="${gpu_avg}" -v ram="${ram_avg}" '
            /"time_sec"/ {gsub(/[ ,]/, "", $2); time=$2}
            /"processing_fps"/ {gsub(/[ ,]/, "", $2); fps=$2}
            /"avg_frame_time_ms"/ {gsub(/[ ,]/, "", $2); frame=$2}
            /"detected_frames"/ {gsub(/[ ,]/, "", $2); detected=$2}
            /"empty_frames"/ {gsub(/[ ,]/, "", $2); empty=$2}
            /"detection_rate"/ {gsub(/[ ,]/, "", $2); rate=$2}
            END {printf "%s,%02d,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", scale, run, time, fps, frame, detected, empty, rate, cpu, gpu, ram}
        ' "${report}" >> "${SUMMARY}"
    done
}

run_scale "1.0" "scale_1.0"
run_scale "0.25" "scale_0.25"

echo "Benchmark complete."
echo "Per-run results: ${RESULT_ROOT}"
echo "Summary CSV:     ${SUMMARY}"
