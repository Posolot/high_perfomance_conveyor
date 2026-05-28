#include "plugin_api.h"
#include <opencv2/imgproc.hpp>

static void process(cv::Mat& frame, const ProcessContext* ctx) {
    (void)ctx;
    if (frame.empty()) return;
    cv::Mat blurred, edges;
    cv::GaussianBlur(frame, blurred, {7, 7}, 1.8);
    cv::Canny(blurred, edges, 80, 160);
    static const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, {3, 3});
    cv::dilate(edges, frame, kernel);
}

extern "C" StagePluginV3* stage_plugin_entry() {
    static StagePluginV3 api = {
        .abi_version = STAGE_PLUGIN_ABI_VERSION,
        .process = process,
        .merge = nullptr
    };
    return &api;
}