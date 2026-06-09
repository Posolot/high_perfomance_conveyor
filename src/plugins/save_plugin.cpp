
#include "plugin_api.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

static std::once_flag dir_created_flag;
static std::string output_dir = "../store_1";   // папка для сохранения

static void process(cv::Mat& frame, const ProcessContext* ctx) {
    if (frame.empty()) {
        std::cerr << "[save_plugin] Empty frame received\n";
        return;
    }

    char filename[256];
    snprintf(filename, sizeof(filename), "%s/frame_%08lu.jpg", output_dir.c_str(),
             static_cast<unsigned long>(ctx->meta->frame_id));

    // Сохраняем изображение
    bool success = cv::imwrite(filename, frame);
    if (!success) {
        std::cerr << "[save_plugin] Failed to save frame " << ctx->meta->frame_id << " to " << filename << std::endl;
    }
}

extern "C" StagePluginV3* stage_plugin_entry() {
    static StagePluginV3 api = {
        .abi_version = STAGE_PLUGIN_ABI_VERSION,
        .process = process,
        .merge = nullptr
    };
    return &api;
}