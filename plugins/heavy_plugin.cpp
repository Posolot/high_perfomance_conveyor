#include "plugin_api.h"
#include <opencv2/imgproc.hpp>

static const char* plugin_name() {
    return "heavy";
}

static void process(cv::Mat& frame) {
    if (frame.empty()) return;

    cv::Mat blurred;
    cv::Mat edges;

    cv::GaussianBlur(frame, blurred, {7, 7}, 1.8);
    cv::Canny(blurred, edges, 80, 160);

    static const cv::Mat kernel =
        cv::getStructuringElement(cv::MORPH_RECT, {3, 3});

    cv::dilate(edges, frame, kernel);
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