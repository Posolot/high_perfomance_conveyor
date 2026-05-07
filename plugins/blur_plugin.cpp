#include "plugin_api.h"
#include <opencv2/imgproc.hpp>

static const char* plugin_name() {
    return "blur";
}

static void process(cv::Mat& frame) {
    if (frame.empty()) return;
    cv::GaussianBlur(frame, frame, {5, 5}, 1.5);
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