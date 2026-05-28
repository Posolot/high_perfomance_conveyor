#ifndef PLUGIN_API_H
#define PLUGIN_API_H

#include <opencv2/opencv.hpp>
#include <stddef.h>

#define STAGE_PLUGIN_ABI_VERSION 3   // увеличиваем версию

struct FrameMetaInfo {
    uint64_t frame_id;
    uint64_t trace_id;
    uint64_t split_id;
    uint16_t branch_id;
    uint16_t expected_branches;
    uint64_t created_ns;
    uint64_t queue_enter_ns;
    const char* source_stage;   // указывает на данные, хранящиеся в конвейере
    uint64_t global_seq;
};

typedef struct {
    const FrameMetaInfo* meta;
    const char* stage_name;
} ProcessContext;

struct StagePluginV3 {
    int abi_version;
    void (*process)(cv::Mat& frame, const ProcessContext* ctx);
    void (*merge)(cv::Mat& base, const cv::Mat** inputs, size_t count);
};

typedef StagePluginV3* (*stage_plugin_entry_fn)();

#endif