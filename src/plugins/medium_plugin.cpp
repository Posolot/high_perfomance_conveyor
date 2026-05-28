#include "plugin_api.h"
#include <opencv2/core.hpp>

static void merge_sum(cv::Mat& base_frame, const cv::Mat* const* inputs, size_t input_count) {
    if (inputs == nullptr || input_count == 0) return;
    for (size_t i = 1; i < input_count; ++i) {
        if (inputs[i] == nullptr) continue;
        cv::add(base_frame, *inputs[i], base_frame);
    }
}

extern "C" StagePluginV3* stage_plugin_entry() {
    static StagePluginV3 api = {
        .abi_version = STAGE_PLUGIN_ABI_VERSION,
        .process = nullptr,
        .merge = merge_sum
    };
    return &api;
}