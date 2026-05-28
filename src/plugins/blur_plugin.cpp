#include "plugin_api.h"
#include <opencv2/imgproc.hpp>

static void process(cv::Mat& frame, const ProcessContext* ctx) {
    (void)ctx;
    if (frame.empty()) return;
    cv::GaussianBlur(frame, frame, {5, 5}, 1.5);
}

extern "C" StagePluginV3* stage_plugin_entry() {
    static StagePluginV3 api = {
        .abi_version = STAGE_PLUGIN_ABI_VERSION,
        .process = process,
        .merge = nullptr
    };
    return &api;
}