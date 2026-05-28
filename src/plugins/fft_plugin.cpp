#include "plugin_api.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

static void process(cv::Mat& frame, const ProcessContext* ctx) {
    (void)ctx;
    if (frame.empty()) return;
    thread_local cv::Mat float_img;
    float_img.create(frame.rows, frame.cols, CV_32F);
    frame.convertTo(float_img, CV_32F);
    frame.create(float_img.rows, float_img.cols, CV_32FC2);
    cv::dft(float_img, frame, cv::DFT_COMPLEX_OUTPUT);
}

extern "C" StagePluginV3* stage_plugin_entry() {
    static StagePluginV3 api = {
        .abi_version = STAGE_PLUGIN_ABI_VERSION,
        .process = process,
        .merge = nullptr
    };
    return &api;
}