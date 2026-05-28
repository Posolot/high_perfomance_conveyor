#include "plugin_api.h"
#include <opencv2/imgproc.hpp>

static void process(cv::Mat& frame, const ProcessContext* ctx) {
    (void)ctx;
    if (frame.empty()) return;
    cv::Mat grad_x, grad_y, mag;
    cv::Sobel(frame, grad_x, CV_32F, 1, 0, 3);
    cv::Sobel(frame, grad_y, CV_32F, 0, 1, 3);
    cv::magnitude(grad_x, grad_y, mag);
    cv::normalize(mag, mag, 0, 255, cv::NORM_MINMAX);
    mag.convertTo(frame, CV_8U);
}

extern "C" StagePluginV3* stage_plugin_entry() {
    static StagePluginV3 api = {
        .abi_version = STAGE_PLUGIN_ABI_VERSION,
        .process = process,
        .merge = nullptr
    };
    return &api;
}