#include "plugin_api.h"
#include <opencv2/opencv.hpp>

static const char* plugin_name() {
    return "sum_merge";
}

static void merge_sum(cv::Mat& base_frame, const cv::Mat* const* inputs, size_t input_count) {
    if (inputs == nullptr || input_count == 0) return;

    for (size_t i = 1; i < input_count; ++i) {
        if (inputs[i] == nullptr) continue;
        cv::add(base_frame, *inputs[i], base_frame);
    }
}

extern "C" const StagePluginV2* stage_plugin_entry() {
    static const StagePluginV2 api{
        STAGE_PLUGIN_ABI_VERSION,
        nullptr,
        &merge_sum,
        plugin_name
    };
    return &api;
}