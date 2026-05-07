#include "plugin_api.h"
#include <opencv2/imgproc.hpp>

static const char* plugin_name() {
    return "rgb2gray";
}

static void process(cv::Mat& frame) {
    if (frame.empty()) return;

    if (frame.channels() == 3) {
        cv::cvtColor(frame, frame, cv::COLOR_BGR2GRAY);
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