import argparse
import json
import time

import cv2
import numpy as np

import os

os.environ["OPENCV_FFMPEG_LOGLEVEL"] = "quiet"

def parse_args():
    """解析命令行参数"""


    parser = argparse.ArgumentParser(
        description="HSV红色区域检测 + 加权质心定位"
    )
 
    parser.add_argument(
    "--scale",
    type=float,
    default=0.5,
    help="检测缩放比例"
    )

    parser.add_argument(
        "--input",
        default="b20241215_193214.mp4",
        help="输入视频路径"
    )

    parser.add_argument(
        "--output",
        default="python_box_detected.mp4",
        help="输出视频路径"
    )

    parser.add_argument(
        "--report",
        default="performance_report.json",
        help="性能报告保存路径"
    )

    # OpenCV HSV:
    # H:0~179
    # S:0~255
    # V:0~255

    parser.add_argument(
        "--lower-hue-max",
        type=int,
        default=9
    )

    parser.add_argument(
        "--upper-hue-min",
        type=int,
        default=171
    )

    parser.add_argument(
        "--min-saturation",
        type=int,
        default=77
    )

    parser.add_argument(
        "--min-value",
        type=int,
        default=77
    )

    parser.add_argument(
        "--median-kernel",
        type=int,
        choices=(3, 5),
        default=3
    )

    parser.add_argument(
        "--box-size",
        type=int,
        default=40
    )

    parser.add_argument(
        "--box-line-width",
        type=int,
        default=2
    )

    return parser.parse_args()


# ================================
# 图像处理
# ================================

def create_red_mask(hsv, args):
    """
    HSV阈值生成红色mask
    """

    h = hsv[:, :, 0]
    s = hsv[:, :, 1]
    v = hsv[:, :, 2]


    mask = (
        ((h <= args.lower_hue_max) |
         (h >= args.upper_hue_min))
        &
        (s >= args.min_saturation)
        &
        (v >= args.min_value)
    )

    return mask



def create_red_intensity_map(
        hsv,
        red_mask,
        kernel_size
):
    """
    生成红色强度图
    """

    value = hsv[:, :, 2]


    intensity = (
        value.astype(np.float32)
        *
        red_mask
    )


    intensity = cv2.medianBlur(
        intensity,
        kernel_size
    )


    return intensity



def calculate_weighted_centroid(
        intensity
):
    """
    根据亮度计算加权质心
    """

    moments = cv2.moments(
        intensity,
        binaryImage=False
    )


    if moments["m00"] <= 0:
        return np.nan, np.nan


    x = moments["m10"] / moments["m00"]
    y = moments["m01"] / moments["m00"]


    return y, x



def draw_centroid_box(
        frame,
        y,
        x,
        size,
        width
):
    """
    绘制检测框
    """

    if np.isnan(y):
        return frame, False


    output = frame.copy()


    half = size // 2


    cx = int(round(x))
    cy = int(round(y))


    cv2.rectangle(
        output,
        (
            cx-half,
            cy-half
        ),
        (
            cx+half,
            cy+half
        ),
        (0,255,0),
        width
    )


    return output, True



def process_frame(frame,args):


    scale=args.scale


    small=cv2.resize(
        frame,
        None,
        fx=scale,
        fy=scale,
        interpolation=cv2.INTER_AREA
    )


    hsv=cv2.cvtColor(
        small,
        cv2.COLOR_BGR2HSV
    )


    mask=create_red_mask(
        hsv,
        args
    )


    intensity=create_red_intensity_map(
        hsv,
        mask,
        args.median_kernel
    )


    y,x=calculate_weighted_centroid(
        intensity
    )


    if not np.isnan(x):

        x=int(x/scale)
        y=int(y/scale)


    output,detected=draw_centroid_box(
        frame,
        y,
        x,
        args.box_size,
        args.box_line_width
    )


    return output,detected


# ================================
# 视频处理
# ================================

def process_video(args):

    cap = cv2.VideoCapture(
        args.input
    )


    if not cap.isOpened():
        raise RuntimeError(
            f"无法打开视频: {args.input}"
        )


    width = int(
        cap.get(cv2.CAP_PROP_FRAME_WIDTH)
    )

    height = int(
        cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
    )

    fps = cap.get(
        cv2.CAP_PROP_FPS
    )

    total_frames = int(
        cap.get(cv2.CAP_PROP_FRAME_COUNT)
    )


    writer = cv2.VideoWriter(
        args.output,
        cv2.VideoWriter_fourcc(*"mp4v"),
        fps,
        (width,height)
    )


    report = {

        "video_info":{

            "name":args.input,
            "width":width,
            "height":height,
            "fps":fps,
            "frames":total_frames

        }

    }


    frame_count = 0
    detected_count = 0


    start = time.time()


    while True:

        ret, frame = cap.read()


        if not ret:
            break


        result, detected = process_frame(
            frame,
            args
        )


        writer.write(result)


        frame_count += 1


        if detected:
            detected_count += 1



    end = time.time()


    elapsed = end-start


    report["performance_metrics"] = {

        "time_sec": round(
            elapsed,
            3
        ),

        "processing_fps": round(
            frame_count / elapsed,
            3
        ),

        "avg_frame_time_ms": round(
            elapsed*1000/frame_count,
            3
        )
    }



    report["detection_metrics"] = {

        "detected_frames": detected_count,

        "empty_frames":
            frame_count-detected_count,

        "detection_rate": round(
            detected_count/frame_count,
            3
        )

    }



    cap.release()
    writer.release()


    return report



def save_report(report, path):

    with open(
        path,
        "w",
        encoding="utf-8"
    ) as f:

        json.dump(
            report,
            f,
            indent=4,
            ensure_ascii=False
        )



def main():

    args = parse_args()


    report = process_video(
        args
    )


    save_report(
        report,
        args.report
    )


    print("\n===== Performance Report =====")

    print(
        f"FPS: {report['performance_metrics']['processing_fps']}"
    )

    print(
        f"Time: {report['performance_metrics']['time_sec']} s"
    )

    print(
        f"Detection rate: {report['detection_metrics']['detection_rate']}"
    )

    print(
        f"Saved: {args.report}"
    )


if __name__ == "__main__":

    main()