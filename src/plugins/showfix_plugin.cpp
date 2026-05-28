#include "plugin_api.h"
#include <iostream>
#include <map>

static std::map<std::string, bool> windows_created;

static void process_frame(cv::Mat& frame, const ProcessContext* ctx) {
    if (frame.empty()) {
        std::cerr << "[display_plugin] Empty frame received\n";
        return;
    }
    const std::string win_name = ctx->stage_name;
    if (!windows_created[win_name]) {
        cv::namedWindow(win_name, cv::WINDOW_NORMAL);
        cv::resizeWindow(win_name, 1920, 1080);
        windows_created[win_name] = true;
    }
    cv::imshow(win_name, frame);
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