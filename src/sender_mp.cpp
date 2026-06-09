#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>

#include <opencv2/opencv.hpp>
#include <zmq.hpp>
#include <capnp/serialize.h>
#include <capnp/message.h>
#include <kj/array.h>

#include "frame.capnp.h"

static uint64_t system_now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}
static bool resized = false;
static bool send_frame(zmq::socket_t& socket,
                       uint64_t frame_id,
                       uint64_t trace_id,
                       uint64_t split_id,
                       uint16_t branch_id,
                       uint16_t expected_branches,
                       uint32_t height,
                       uint32_t width,
                       uint16_t channels,
                       const uint8_t* pixels,
                       size_t pixel_bytes)
{
    capnp::MallocMessageBuilder message;
    FrameEnvelope::Builder env = message.initRoot<FrameEnvelope>();

    auto meta = env.initMeta();
    meta.setFrameId(frame_id);
    meta.setTraceId(trace_id);
    meta.setSplitId(split_id);
    meta.setBranchId(branch_id);
    meta.setExpectedBranches(expected_branches);
    meta.setSourceStage("video_source");
    meta.setCreatedNs(system_now_ns());

    env.setHeight(height);
    env.setWidth(width);
    env.setChannels(channels);

    auto blob = env.initPixels(pixel_bytes);
    std::memcpy(blob.begin(), pixels, pixel_bytes);

    kj::Array<capnp::word> flat = capnp::messageToFlatArray(message);
    auto bytes = flat.asBytes();

    zmq::message_t out(bytes.size());
    std::memcpy(out.data(), bytes.begin(), bytes.size());
    auto res = socket.send(out, zmq::send_flags::dontwait);
    return res.has_value();
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <video_file> [sleep_ms] [--show]\n";
            std::cerr << "  video_file - path to video file (mp4, mov, avi, etc.)\n";
            std::cerr << "  sleep_ms   - milliseconds between frames (default: 25, 0=as fast as possible)\n";
            std::cerr << "  --show     - show original frames in a window (optional)\n";
            return 1;
        }

        const std::string video_path = argv[1];
        int sleep_ms = 25;
        bool show_window = false;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--show") {
                show_window = true;
            } else {
                sleep_ms = std::stoi(arg);
                if (sleep_ms < 0) sleep_ms = 0;
            }
        }

        cv::VideoCapture cap(video_path);
        if (!cap.isOpened()) {
            std::cerr << "[ERROR] Cannot open video: " << video_path << std::endl;
            return 1;
        }

        double fps = cap.get(cv::CAP_PROP_FPS);
        int width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
        int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
        int total_frames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));

        std::cout << "Video: " << width << "x" << height << ", fps=" << fps << ", total frames=" << total_frames << std::endl;
        if (sleep_ms == 0 && fps > 0) {
            // Если sleep_ms=0, используем реальную частоту кадров видео
            sleep_ms = static_cast<int>(1000.0 / fps);
            std::cout << "Auto frame delay based on video FPS: " << sleep_ms << " ms" << std::endl;
        }

        zmq::context_t ctx(1);
        zmq::socket_t socket(ctx, zmq::socket_type::push);
        socket.set(zmq::sockopt::sndhwm, 10000);
        socket.bind("tcp://127.0.0.1:5558");

        cv::Mat frame;
        uint64_t frame_counter = 0;
        using clock = std::chrono::steady_clock;
        auto next_time = clock::now();

        while (true) {
            if (!cap.read(frame)) {
                // Если видео закончилось, начинаем сначала
                cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                frame_counter = 0;
                if (!cap.read(frame)) break;
            }




            bool sent = send_frame(socket,
                                   frame_counter, frame_counter, frame_counter,
                                   0, 1,
                                   frame.rows, frame.cols, 3,
                                   frame.data, frame.total() * frame.elemSize());

            if (!sent) {
                std::cerr << "[WARN] Failed to send frame " << frame_counter << std::endl;
            }
            if (show_window) {
                cv::imshow("Sender Original", frame);
                if (!resized) {
                    cv::resizeWindow("Sender Original", 1280, 720);
                    resized = true;
                }
                cv::waitKey(1);
            }
            frame_counter++;

            if (sleep_ms > 0) {
                next_time += std::chrono::milliseconds(sleep_ms);
                std::this_thread::sleep_until(next_time);
            }
        }

        std::cout << "Sent " << frame_counter << " frames (looped)." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}