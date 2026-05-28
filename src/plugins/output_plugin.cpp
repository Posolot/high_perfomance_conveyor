#include "plugin_api.h"
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

static const char* plugin_name() {
    return "output";
}

static void ensure_output_dir() {
    const std::string output_dir = "completed";
    if (!fs::exists(output_dir)) {
        fs::create_directories(output_dir);
    }
}

static std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t_now), "%Y%m%d_%H%M%S");
    oss << "_" << std::setfill('0') << std::setw(3) << millis.count();
    return oss.str();
}

static void process(cv::Mat& frame) {
    if (frame.empty()) return;

    ensure_output_dir();

    std::string timestamp = get_timestamp();
    std::string filename = "completed/frame_" + timestamp + ".png";

    if (!cv::imwrite(filename, frame)) {
        static int frame_counter = 0;
        std::ostringstream oss;
        oss << "completed/frame_" << std::setfill('0') << std::setw(8) << (frame_counter++) << ".png";
        cv::imwrite(oss.str(), frame);
    }
}

extern "C" const StagePluginV2* stage_plugin_entry() {
    static const StagePluginV2 api{
        STAGE_PLUGIN_ABI_VERSION,
        &process,
        nullptr,
        plugin_name
    };
    return &api;
}