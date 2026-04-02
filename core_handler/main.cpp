#include "../taskflow/taskflow/taskflow.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip> 

void do_work(const std::string& name, int ms) {
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << name << " started on thread " << std::this_thread::get_id() << "\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(ms)); // имитация нагрузки

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << name << " finished on thread " << std::this_thread::get_id() 
              << " | duration: " << duration << " ms\n";
}

int main() {

    tf::Executor executor;
    tf::Taskflow taskflow;

    auto A = taskflow.emplace([](){ do_work("Task A", 500); });
    auto B = taskflow.emplace([](){ do_work("Task B", 1000); });
    auto C = taskflow.emplace([](){ do_work("Task C", 700); });
    auto D = taskflow.emplace([](){ do_work("Task D", 300); });
    auto F = taskflow.emplace([](){ do_work("Task F", 800); });

    // зависимости
    A.precede(B, C);
    B.precede(C);
    B.precede(F);
    F.precede(D);
    C.precede(D);

    // запуск графа
    executor.run(taskflow).wait();

    // Экспорт графа в DOT файл для визуализации
    std::ofstream dot("taskflow_graph.dot");
    taskflow.dump(dot);
    dot.close();

    std::cout << "\nGraph exported to taskflow_graph.dot (можно открыть через Graphviz)\n";

    return 0;
}
