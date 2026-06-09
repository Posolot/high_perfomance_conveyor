#include "plugin_api.h"
#include <iostream>
#include <mutex>

static std::once_flag window_created;

static void process_frame(cv::Mat& frame, const ProcessContext* ctx) {
    (void)ctx; // метаданные не используются
    if (frame.empty()) {
        std::cerr << "[display_plugin] Empty frame received\n";
        return;
    }
    std::call_once(window_created, []() {
        cv::namedWindow("Pipeline Output", cv::WINDOW_NORMAL);
        cv::resizeWindow("Pipeline Output", 1280, 720);
    });
    cv::imshow("Pipeline Output", frame);
    cv::waitKey(1);
}

extern "C" StagePluginV3* stage_plugin_entry() {
    static StagePluginV3 api = {
        .abi_version = STAGE_PLUGIN_ABI_VERSION,
        .process = process_frame,
        .merge = nullptr
    };
    return &api;
}