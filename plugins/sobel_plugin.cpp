#include "plugin_api.h"
#include <opencv2/imgproc.hpp>

static const char* plugin_name() {
    return "sobel_magnitude";
}

static void process(cv::Mat& frame) {
    if (frame.empty()) return;

    cv::Mat grad_x, grad_y;
    cv::Mat mag;

    cv::Sobel(frame, grad_x, CV_32F, 1, 0, 3);
    cv::Sobel(frame, grad_y, CV_32F, 0, 1, 3);
    cv::magnitude(grad_x, grad_y, mag);
    cv::normalize(mag, mag, 0, 255, cv::NORM_MINMAX);
    mag.convertTo(frame, CV_8U);
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