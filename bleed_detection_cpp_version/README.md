# bleed_detection C++ 版本

这是基于 OpenCV 和 C++17 的可读性优先实现。默认参数与 Python 版本保持一致，并支持 `--scale 0.25` 下采样检测。

## 编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 运行

```bash
./build/bleed_detection \
  --input b20241215_193214.mp4 \
  --output cpp_box_detected.mp4 \
  --report cpp_performance_report.json \
  --scale 0.25
```
