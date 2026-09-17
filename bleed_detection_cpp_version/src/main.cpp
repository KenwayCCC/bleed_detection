#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    double scale = 0.5;
    std::string input = "b20241215_193214.mp4";
    std::string output = "cpp_box_detected.mp4";
    std::string report = "cpp_performance_report.json";
    int lower_hue_max = 9;
    int upper_hue_min = 171;
    int min_saturation = 77;
    int min_value = 77;
    int median_kernel = 3;
    int box_size = 40;
    int box_line_width = 2;
    bool no_output = false;
};

struct DetectionResult {
    bool detected = false;
    double x = std::numeric_limits<double>::quiet_NaN();
    double y = std::numeric_limits<double>::quiet_NaN();
};

struct Metrics {
    int frames = 0;
    int detected_frames = 0;
    double elapsed_seconds = 0.0;
};

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "  --scale VALUE              Detection scale, default 0.5\n"
              << "  --input PATH               Input video\n"
              << "  --output PATH              Output video\n"
              << "  --report PATH              JSON report\n"
              << "  --lower-hue-max VALUE      Default 9\n"
              << "  --upper-hue-min VALUE      Default 171\n"
              << "  --min-saturation VALUE     Default 77\n"
              << "  --min-value VALUE          Default 77\n"
              << "  --median-kernel 3|5        Default 3\n"
              << "  --box-size VALUE           Default 40\n"
              << "  --box-line-width VALUE     Default 2\n"
              << "  --no-output                Do not save an output video\n"
              << "  --help                     Show this help\n";
}

template <typename T>
T parse_value(const std::string& value, const std::string& option) {
    std::istringstream stream(value);
    T result{};
    if (!(stream >> result) || !stream.eof()) {
        throw std::invalid_argument("Invalid value for " + option + ": " + value);
    }
    return result;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (argument == "--no-output") {
            options.no_output = true;
            continue;
        }
        if (i + 1 >= argc) {
            throw std::invalid_argument("Missing value for " + argument);
        }
        const std::string value = argv[++i];
        if (argument == "--scale") options.scale = parse_value<double>(value, argument);
        else if (argument == "--input") options.input = value;
        else if (argument == "--output") options.output = value;
        else if (argument == "--report") options.report = value;
        else if (argument == "--lower-hue-max") options.lower_hue_max = parse_value<int>(value, argument);
        else if (argument == "--upper-hue-min") options.upper_hue_min = parse_value<int>(value, argument);
        else if (argument == "--min-saturation") options.min_saturation = parse_value<int>(value, argument);
        else if (argument == "--min-value") options.min_value = parse_value<int>(value, argument);
        else if (argument == "--median-kernel") options.median_kernel = parse_value<int>(value, argument);
        else if (argument == "--box-size") options.box_size = parse_value<int>(value, argument);
        else if (argument == "--box-line-width") options.box_line_width = parse_value<int>(value, argument);
        else throw std::invalid_argument("Unknown option: " + argument);
    }
    if (options.scale <= 0.0 || options.scale > 1.0) throw std::invalid_argument("--scale must be in (0, 1].");
    if (options.median_kernel != 3 && options.median_kernel != 5) throw std::invalid_argument("--median-kernel must be 3 or 5.");
    return options;
}

cv::Mat create_red_mask(const cv::Mat& hsv, const Options& options) {
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);
    cv::Mat hue_low, hue_high, saturation_ok, value_ok, mask;
    cv::inRange(channels[0], 0, options.lower_hue_max, hue_low);
    cv::inRange(channels[0], options.upper_hue_min, 179, hue_high);
    cv::bitwise_or(hue_low, hue_high, mask);
    cv::inRange(channels[1], options.min_saturation, 255, saturation_ok);
    cv::inRange(channels[2], options.min_value, 255, value_ok);
    cv::bitwise_and(mask, saturation_ok, mask);
    cv::bitwise_and(mask, value_ok, mask);
    return mask;
}

DetectionResult detect_centroid(const cv::Mat& frame, const Options& options) {
    cv::Mat resized, hsv, red_mask, value_channel, intensity;
    cv::resize(frame, resized, cv::Size(), options.scale, options.scale, cv::INTER_AREA);
    cv::cvtColor(resized, hsv, cv::COLOR_BGR2HSV);
    red_mask = create_red_mask(hsv, options);
    cv::extractChannel(hsv, value_channel, 2);
    value_channel.convertTo(intensity, CV_32F);
    intensity.setTo(0.0F, red_mask == 0);
    cv::medianBlur(intensity, intensity, options.median_kernel);

    const cv::Moments moments = cv::moments(intensity, false);
    if (moments.m00 <= 0.0) return {};

    // Keep the same coordinate convention as the Python implementation.
    const double small_x = moments.m10 / moments.m00;
    const double small_y = moments.m01 / moments.m00;
    return {true, small_x / options.scale, small_y / options.scale};
}

void draw_centroid_box(cv::Mat& frame, const DetectionResult& result, const Options& options) {
    if (!result.detected) return;
    // Match Python's int(x / scale) truncation semantics.
    const cv::Point center(static_cast<int>(result.x), static_cast<int>(result.y));
    const int half = options.box_size / 2;
    // Point-to-point drawing matches Python's inclusive rectangle endpoints.
    cv::rectangle(frame, cv::Point(center.x - half, center.y - half),
                  cv::Point(center.x + half, center.y + half), cv::Scalar(0, 255, 0), options.box_line_width);
}

void write_report(const Options& options, const cv::Size& size, double fps, Metrics metrics) {
    const double frame_time_ms = metrics.frames > 0 ? metrics.elapsed_seconds * 1000.0 / metrics.frames : 0.0;
    const double detection_rate = metrics.frames > 0 ? static_cast<double>(metrics.detected_frames) / metrics.frames : 0.0;
    std::ofstream report(options.report);
    if (!report) throw std::runtime_error("Cannot write report: " + options.report);
    report << std::fixed << std::setprecision(6)
           << "{\n  \"video_info\": {\n"
           << "    \"name\": \"" << options.input << "\",\n"
           << "    \"width\": " << size.width << ",\n    \"height\": " << size.height << ",\n"
           << "    \"fps\": " << fps << ",\n    \"frames\": " << metrics.frames << "\n  },\n"
           << "  \"performance_metrics\": {\n    \"time_sec\": " << metrics.elapsed_seconds
           << ",\n    \"processing_fps\": " << (metrics.elapsed_seconds > 0 ? metrics.frames / metrics.elapsed_seconds : 0.0)
           << ",\n    \"avg_frame_time_ms\": " << frame_time_ms << "\n  },\n"
           << "  \"detection_metrics\": {\n    \"detected_frames\": " << metrics.detected_frames
           << ",\n    \"empty_frames\": " << metrics.frames - metrics.detected_frames
           << ",\n    \"detection_rate\": " << detection_rate << "\n  }\n}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        cv::VideoCapture capture(options.input);
        if (!capture.isOpened()) throw std::runtime_error("Cannot open input video: " + options.input);

        const double input_fps = capture.get(cv::CAP_PROP_FPS);
        const cv::Size frame_size(static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH)),
                                  static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT)));
        const int codec = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
        cv::VideoWriter writer;
        if (!options.no_output) {
            writer.open(options.output, codec, input_fps, frame_size);
            if (!writer.isOpened()) throw std::runtime_error("Cannot open output video: " + options.output);
        }

        Metrics metrics;
        cv::Mat frame;
        const auto start = std::chrono::steady_clock::now();
        while (capture.read(frame)) {
            const DetectionResult result = detect_centroid(frame, options);
            draw_centroid_box(frame, result, options);
            if (!options.no_output) writer.write(frame);
            ++metrics.frames;
            metrics.detected_frames += result.detected ? 1 : 0;
        }
        metrics.elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        write_report(options, frame_size, input_fps, metrics);
        std::cout << "Processed " << metrics.frames << " frames in " << metrics.elapsed_seconds
                  << " s (" << metrics.frames / metrics.elapsed_seconds << " FPS).\n"
                  << "Detected frames: " << metrics.detected_frames << "/" << metrics.frames << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
