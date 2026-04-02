#include <iostream>
#include <fstream>
#include <unordered_map>
#include <functional>

#include <opencv2/opencv.hpp>
#include "taskflow/taskflow/taskflow.hpp"
#include <zmq.hpp>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

struct Frame {
    cv::Mat mat;
};

// ====== Обработки ======
void erode(Frame& f) {
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::erode(f.mat, f.mat, kernel);
}

void rgb2gray(Frame& f) {
    if (f.mat.channels() == 3) {
        cv::cvtColor(f.mat, f.mat, cv::COLOR_BGR2GRAY);
    }
}

void gaussian(Frame& f) {
    cv::GaussianBlur(f.mat, f.mat, cv::Size(5, 5), 1.5);
}

// ====== Реестр ======
std::unordered_map<std::string, std::function<void(Frame&)>> registry = {
    {"erode", erode},
    {"rgb2gray", rgb2gray},
    {"gaussian", gaussian}
};

// ====== Построение Taskflow из JSON ======
void build_taskflow_from_json(
    const json& j,
    tf::Taskflow& taskflow,
    std::unordered_map<std::string, tf::Task>& tasks,
    Frame& frame
) {
    // 1. Создаем задачи
    for (const auto& stage : j["stages"]) {
        std::string name = stage["name"];
        std::string callable = stage["callable"];

        tasks[name] = taskflow.emplace([&, callable, name]() {

            auto it = registry.find(callable);
            if (it != registry.end()) {
                it->second(frame);
            } else {
                std::cerr << "Unknown callable: " << callable << "\n";
            }
        }).name(name);
    }

    // 2. Связываем задачи
    for (const auto& conn : j["connections"]) {
        std::string from = conn["from"];
        for (const auto& to_item : conn["to"]) {
            std::string to = to_item.get<std::string>();

            if (tasks.count(from) && tasks.count(to)) {
                tasks[from].precede(tasks[to]);
            } else {
                std::cerr << "Invalid connection: " << from << " -> " << to << "\n";
            }
        }
    }
}

int main() {
    std::cout << std::unitbuf;
    const int HEIGHT = 480;
    const int WIDTH  = 640;

    // ====== Читаем config ======
    std::ifstream file("config.json");
    if (!file) {
        std::cerr << "Failed to open config.json\n";
        return 1;
    }

    json j;
    file >> j;

    // ====== ZeroMQ ======
    zmq::context_t context(1);
    zmq::socket_t receiver(context, zmq::socket_type::pull);
    receiver.connect("tcp://127.0.0.1:5558");

    // ====== Taskflow ======
    tf::Taskflow taskflow;
    std::unordered_map<std::string, tf::Task> tasks;
    Frame frame;

    build_taskflow_from_json(j, taskflow, tasks, frame);

    tf::Executor executor;

    int frame_count = 0;
    int fps_counter = 0;
    const int FPS_WINDOW = 100;  // каждые 100 кадров
    auto start_time = std::chrono::high_resolution_clock::now();

    while (true) {
        zmq::message_t msg;
        auto res = receiver.recv(msg, zmq::recv_flags::none);
        if (!res) {
            std::cerr << "[ZeroMQ] Failed to receive frame\n";
            continue;
        }

        if (msg.size() != HEIGHT * WIDTH) {
            std::cerr << "Wrong frame size\n";
            continue;
        }

        frame.mat = cv::Mat(HEIGHT, WIDTH, CV_8U, msg.data()).clone();

        // ====== Запуск конвейера ======
        executor.run(taskflow).wait();

        frame_count++;
        fps_counter++;

        // ====== Подсчёт FPS каждые 100 кадров ======
        if (fps_counter >= FPS_WINDOW) {
            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end_time - start_time;
            double fps = fps_counter / elapsed.count();
            std::cout << "[FPS] Processed " << fps_counter 
                      << " frames in " << elapsed.count() 
                      << " s -> FPS: " << fps << "\n";
            fps_counter = 0;
            start_time = std::chrono::high_resolution_clock::now();
        }
    }

    return 0;
}