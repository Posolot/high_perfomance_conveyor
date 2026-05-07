#include "plugin_api.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

static const char* plugin_name() {
    return "fft_fast";
}

static void process(cv::Mat& frame) {
    if (frame.empty()) return;

    // Кэш на поток: переиспользуется между вызовами, без лишних malloc/free.
    thread_local cv::Mat float_img;

    // 1) Переводим исходный кадр в float один раз, в переиспользуемый буфер.
    //    Это дешевле, чем держать несколько промежуточных Mat.
    float_img.create(frame.rows, frame.cols, CV_32F);
    frame.convertTo(float_img, CV_32F);

    // 2) Выделяем память под результат сразу в самом frame.
    //    DFT с DFT_COMPLEX_OUTPUT возвращает комплексный спектр: CV_32FC2.
    frame.create(float_img.rows, float_img.cols, CV_32FC2);

    // 3) Пишем FFT сразу в frame.
    //    Никакого extra copyTo после этого.
    cv::dft(float_img, frame, cv::DFT_COMPLEX_OUTPUT);
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