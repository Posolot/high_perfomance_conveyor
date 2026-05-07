#pragma once

#include <opencv2/opencv.hpp>
#include <cstdint>
#include <cstddef>

#define STAGE_PLUGIN_ABI_VERSION 2

struct StagePluginV2 {
    uint32_t abi_version;
    void (*process)(cv::Mat& frame);
    void (*merge)(cv::Mat& base_frame, const cv::Mat* const* inputs, size_t input_count);
    const char* (*name)();
};

using stage_plugin_entry_fn = const StagePluginV2* (*)();