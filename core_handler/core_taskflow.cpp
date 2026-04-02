#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include <thread>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

#include <opencv2/opencv.hpp>
#include "../taskflow/taskflow/taskflow.hpp"
#include "nlohmann/json.hpp"
#include <zmq.hpp>

// --------------------------------------------
// Frame структура
struct Frame {
    int id;
    cv::Mat mat;
};

// --------------------------------------------
// POSIX Shared Memory буфер
struct ShmBuffer {
    int width;
    int height;
    int type;
    size_t size;
    void* data;

    ShmBuffer(int w, int h, int t) : width(w), height(h), type(t) {
        size = width * height * CV_ELEM_SIZE(type);
        int fd = shm_open("/frame_shm", O_CREAT | O_RDWR, 0666);
        ftruncate(fd, size);
        data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
    }

    ~ShmBuffer() {
        munmap(data, size);
        shm_unlink("/frame_shm");
    }

    void write(const cv::Mat& mat) { memcpy(data, mat.data, size); }
    void read(cv::Mat& mat) { memcpy(mat.data, data, size); }
};

// --------------------------------------------
// Функции обработки
void erode(Frame& f) {
    cv::erode(f.mat, f.mat, cv::Mat());
    std::cout << "[Frame " << f.id << "] Erode\n";
}

void rgb2gray(Frame& f) {
    if(f.mat.channels() == 3)
        cv::cvtColor(f.mat, f.mat, cv::COLOR_BGR2GRAY);
    std::cout << "[Frame " << f.id << "] Gray\n";
}

void gaussian(Frame& f) {
    cv::GaussianBlur(f.mat, f.mat, cv::Size(5,5), 1.5);
    std::cout << "[Frame " << f.id << "] Gaussian blur\n";
}

// --------------------------------------------
// Registry
std::unordered_map<std::string,std::function<void(Frame&)>> registry = {
    {"erode", erode},
    {"rgb2gray", rgb2gray},
    {"gaussian", gaussian}
};

// --------------------------------------------
// Построение Taskflow DAG из JSON
void build_taskflow_from_json(
    const nlohmann::json& j,
    tf::Taskflow& taskflow,
    std::unordered_map<std::string, tf::Task>& tasks)
{
    for(auto& stage : j["stages"]) {
        std::string name = stage["name"];
        std::string callable = stage["callable"];
        tasks[name] = taskflow.emplace([callable](Frame& f){
            auto it = registry.find(callable);
            if(it != registry.end()) it->second(f);
            else std::cerr << "Unknown callable: " << callable << std::endl;
        }).name(name);
    }

    // Создаём зависимости
    for(auto& conn : j["connections"]) {
        std::string from = conn["from"];
        for(auto& to : conn["to"]) {
            if(tasks.count(from) && tasks.count(to.get<std::string>()))
                tasks[from].precede(tasks[to.get<std::string>()]);
        }
    }
}

// --------------------------------------------
// Получение кадра из ZMQ
Frame get_frame_from_zmq(zmq::socket_t& socket, int id, int w, int h, int type) {
    zmq::message_t msg;
    socket.recv(msg, zmq::recv_flags::none);
    Frame f{id, cv::Mat(h, w, type)};
    memcpy(f.mat.data, msg.data(), msg.size());
    return f;
}

// --------------------------------------------
// Main
int main() {
    // ZMQ setup
    zmq::context_t context;
    zmq::socket_t socket(context, zmq::socket_type::pull);
    socket.connect("tcp://localhost:5558");

    // Чтение JSON-конфига
    std::ifstream file("config.json");
    if(!file) { std::cerr << "Failed to open config.json\n"; return 1; }
    nlohmann::json j; file >> j;

    tf::Taskflow taskflow;
    tf::Executor executor;
    std::unordered_map<std::string, tf::Task> tasks;
    build_taskflow_from_json(j, taskflow, tasks);

    // Shared memory для передачи кадра между этапами
    ShmBuffer shm(640, 480, CV_8UC3);

    int frame_id = 0;
    while(true) {
        Frame f = get_frame_from_zmq(socket, frame_id++, 640, 480, CV_8UC3);

        // Пишем кадр в SHM
        shm.write(f.mat);

        // Запуск Taskflow с кадром
        executor.run(taskflow, f).wait();

        // Читаем кадр из SHM после обработки
        shm.read(f.mat);

        // Сохраняем для проверки
        cv::imwrite("frame_" + std::to_string(f.id) + ".png", f.mat);
    }

    return 0;
}
