#include "plugin_api.h"
#include <opencv2/imgproc.hpp>

static const char* plugin_name() {
    return "erode";
}

static void process(cv::Mat& frame) {
    if (frame.empty()) return;

    static const cv::Mat kernel =
        cv::getStructuringElement(cv::MORPH_RECT, {3, 3});

    cv::erode(frame, frame, kernel);
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