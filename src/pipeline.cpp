#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <array>
#include <chrono>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <memory>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <unordered_map>
#include <cctype>
#include <cmath>
#include <unistd.h>
#include <limits>

#include <opencv2/opencv.hpp>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <dlfcn.h>

#include <capnp/serialize.h>
#include <capnp/message.h>
#include <kj/array.h>

#include "../plugin_api.h"
#include "frame.capnp.h"

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

using json = nlohmann::json;

// ================= LATENCY BUFFERS =================
constexpr size_t LATENCY_RING_CAP = 1u << 18; // 262144 samples per ring

template <size_t Capacity>
class LatencyRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    LatencyRing() {
        for (auto& x : data_) {
            x.store(0, std::memory_order_relaxed);
        }
    }

    void add(uint64_t ns) {
        const size_t idx = write_idx_.fetch_add(1, std::memory_order_relaxed);
        data_[idx & mask_].store(ns, std::memory_order_relaxed);
    }

    size_t snapshot(std::vector<uint64_t>& out) const {
        const size_t w = write_idx_.load(std::memory_order_acquire);
        const size_t n = std::min(w, Capacity);
        out.resize(n);

        const size_t start = (w >= n) ? (w - n) : 0;
        for (size_t i = 0; i < n; ++i) {
            out[i] = data_[(start + i) & mask_].load(std::memory_order_relaxed);
        }
        return n;
    }

    size_t count() const {
        return std::min(write_idx_.load(std::memory_order_relaxed), Capacity);
    }

private:
    static constexpr size_t mask_ = Capacity - 1;
    std::array<std::atomic<uint64_t>, Capacity> data_{};
    std::atomic<size_t> write_idx_{0};
};

struct LatencyStats {
    size_t samples = 0;
    double avg_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
    double max_ms = 0.0;
    double tail_gt_10ms_pct = 0.0;
    double tail_gt_50ms_pct = 0.0;
};

static inline double ns_to_ms(uint64_t ns) {
    return static_cast<double>(ns) / 1e6;
}

static inline LatencyStats summarize_latency(std::vector<uint64_t> samples_ns) {
    LatencyStats s{};
    s.samples = samples_ns.size();
    if (samples_ns.empty()) {
        return s;
    }

    std::sort(samples_ns.begin(), samples_ns.end());

    auto percentile = [&](double p) -> uint64_t {
        const double rank = p * static_cast<double>(samples_ns.size() - 1);
        size_t idx = static_cast<size_t>(std::llround(rank));
        if (idx >= samples_ns.size()) idx = samples_ns.size() - 1;
        return samples_ns[idx];
    };

    long double sum = 0.0L;
    size_t tail10 = 0;
    size_t tail50 = 0;

    for (uint64_t x : samples_ns) {
        sum += static_cast<long double>(x);
        if (x > 10'000'000ULL) ++tail10; // > 10 ms
        if (x > 50'000'000ULL) ++tail50; // > 50 ms
    }

    s.avg_ms = static_cast<double>(sum / static_cast<long double>(samples_ns.size())) / 1e6;
    s.p50_ms = ns_to_ms(percentile(0.50));
    s.p95_ms = ns_to_ms(percentile(0.95));
    s.p99_ms = ns_to_ms(percentile(0.99));
    s.max_ms = ns_to_ms(samples_ns.back());
    s.tail_gt_10ms_pct = 100.0 * static_cast<double>(tail10) / static_cast<double>(samples_ns.size());
    s.tail_gt_50ms_pct = 100.0 * static_cast<double>(tail50) / static_cast<double>(samples_ns.size());
    return s;
}

// ================= FRAME META =================

// ================= FRAME =================
struct Frame {
    FrameMetaInfo meta;
    cv::Mat mat;
};

// ================= CPU PINNING =================
#if defined(__linux__)
inline void pin_current_thread_to_cpu(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "[WARN] Failed to pin thread to CPU " << cpu_id << "\n";
    }
}
#else
inline void pin_current_thread_to_cpu(int) {}
#endif

// ================= CLOCK HELPERS =================
inline uint64_t steady_now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

// ================= LOCK-FREE MPMC QUEUE =================
template <typename T, size_t Capacity>
class MPMCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    MPMCQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    bool push(const T& data) {
        size_t pos = tail_.load(std::memory_order_relaxed);
        for (;;) {
            Cell* cell = &buffer_[pos & mask_];
            size_t seq = cell->seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    cell->data = data;
                    cell->seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    bool pop(T& data) {
        size_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            Cell* cell = &buffer_[pos & mask_];
            size_t seq = cell->seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (diff == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    data = cell->data;
                    cell->seq.store(pos + Capacity, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }

private:
    struct Cell {
        std::atomic<size_t> seq;
        T data;
    };
    static constexpr size_t mask_ = Capacity - 1;
    std::array<Cell, Capacity> buffer_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

// ================= CAPACITIES =================
constexpr size_t STAGE_QUEUE_CAP = 1024;
constexpr size_t FREE_SLOTS_CAP = 1024;
constexpr uint64_t MERGE_TIMEOUT_NS = 5'000'000'000ULL;

// ================= CONFIG STRUCTS =================
struct RuntimeConfig {
    int physical_cores = 2;
    int logical_cpus = 4;
    int cpu_budget = 0;
    int frame_h = 1920;
    int frame_w = 1080;
    int frame_channels = 1;
    int buffer_size = 60;
    std::vector<int> cpu_ids;

    std::string ip = "127.0.0.1";
    int port = 5558;
    std::string protocol = "tcp";
    std::string socket_type = "PULL";
};

struct StageSpec {
    std::string name;
    std::string callable;
    std::string plugin_path;
    std::vector<std::string> next;
    int initial_workers = 1;
    int min_workers = 1;
    int max_workers = 0;
    int queue_threshold = 8;
    long long time_threshold_ns = 5'000'000;
};

// ================= GLOBAL CONFIG =================
RuntimeConfig g_cfg;

// ================= GLOBAL BUFFER =================
std::vector<Frame> buffer;

// ================= GLOBAL OUTPUT TRACKING =================
struct OrderedOutputItem {
    uint64_t global_seq = 0;
    uint64_t frame_id = 0;
    uint64_t created_ns = 0;
    int slot = -1;   // добавлен слот
};

// ================= QUIET MODE =================
bool g_quiet = false;

std::atomic<uint64_t> g_global_seq_counter{0};
LatencyRing<LATENCY_RING_CAP> g_e2e_latency_ns;

// ================= FREE SLOTS =================
MPMCQueue<int, FREE_SLOTS_CAP> free_slots;
std::atomic<int> free_slots_count{0};
std::atomic<long long> input_frames{0};
std::atomic<long long> completed_frames{0};
std::atomic<int> dropped_frames{0};
std::atomic<bool> g_failed{false};

// ================= SPLIT IDS =================
std::atomic<uint64_t> g_next_split_id{1};
inline uint64_t next_split_id() {
    return g_next_split_id.fetch_add(1, std::memory_order_relaxed);
}

// ================= PLUGIN LOADING =================
struct LoadedPlugin {
    void* handle = nullptr;
    const StagePluginV3* api = nullptr;
};

LoadedPlugin load_plugin(const std::string& path, bool require_process, bool require_merge) {
    LoadedPlugin p{};
    p.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!p.handle) {
        throw std::runtime_error(std::string("dlopen failed for '") + path + "': " + dlerror());
    }
    dlerror();
    auto entry = reinterpret_cast<stage_plugin_entry_fn>(dlsym(p.handle, "stage_plugin_entry"));
    const char* err = dlerror();
    if (err) {
        throw std::runtime_error(std::string("dlsym failed for '") + path + "': " + err);
    }
    p.api = entry();
    if (!p.api) {
        throw std::runtime_error("plugin entry returned null: " + path);
    }
    if (p.api->abi_version != STAGE_PLUGIN_ABI_VERSION) {
    throw std::runtime_error("plugin ABI mismatch: expected " + std::to_string(STAGE_PLUGIN_ABI_VERSION) + ", got " + std::to_string(p.api->abi_version));
    }   
    if (require_process && !p.api->process) {
        throw std::runtime_error("plugin process() is null: " + path);
    }
    if (require_merge && !p.api->merge) {
        throw std::runtime_error("plugin merge() is null: " + path);
    }
    return p;
}

// ================= LATENCY HELPERS =================
[[noreturn]] void fail_fast_drop(const char* where, uint64_t frame_id, uint64_t global_seq) {
    bool expected = false;
    if (g_failed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        std::cerr << "\n[FATAL] Frame drop detected at " << where
                  << " | frame_id=" << frame_id
                  << " | global_seq=" << global_seq
                  << "\nPipeline stopped.\n";
        std::cerr.flush();
        std::fflush(stderr);
    }
    std::_Exit(EXIT_FAILURE);
}

// ================= STAGE =================
struct MergeGroup {
    std::vector<int> slots;
    uint64_t seen_mask = 0;
    uint64_t expected_mask = 0;
    uint16_t expected_branches = 0;
    uint64_t trace_id = 0;
    uint64_t split_id = 0;
    uint64_t first_seen_steady_ns = 0;
};

struct Stage {
    std::string name;
    std::string callable;
    std::string plugin_path;
    std::string kind = "normal";

    LoadedPlugin plugin;

    MPMCQueue<int, STAGE_QUEUE_CAP> queue;
    std::atomic<int> queue_depth{0};

    std::atomic<int> worker_count{0};
    int min_workers{1};
    int initial_workers{1};
    int max_workers{1};

    int queue_threshold{8};
    long long time_threshold_ns{5'000'000};

    std::atomic<long long> ema_time_ns{0};

    std::vector<Stage*> next_stages;

    std::atomic<long long> processed_total{0};
    std::atomic<long long> time_ns_total{0};

    // Per-stage latency distributions
    LatencyRing<LATENCY_RING_CAP> process_latency_ns;
    LatencyRing<LATENCY_RING_CAP> queue_wait_latency_ns;

    std::mutex merge_mutex;
    std::unordered_map<uint64_t, MergeGroup> pending_merges;

    // Для терминальных стадий – свой reorder buffer
    MPMCQueue<OrderedOutputItem, 4096> output_queue;
    std::unique_ptr<std::thread> reorder_thread;
    std::atomic<bool> reorder_stop{false};
};

// ================= GLOBAL CONTROL =================
std::atomic<int> g_total_runtime_workers{0};
int g_worker_budget = 0;
std::atomic<size_t> g_cpu_rr{0};

// ================= PIPELINE VIEW =================
std::vector<Stage*> g_pipeline_stages;

// ================= HELPER FUNCTIONS =================
inline int next_cpu_id() {
    if (g_cfg.cpu_ids.empty()) return 0;
    const size_t idx = g_cpu_rr.fetch_add(1, std::memory_order_relaxed);
    return g_cfg.cpu_ids[idx % g_cfg.cpu_ids.size()];
}

inline std::string to_lower_copy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

inline uint64_t expected_mask_for(size_t branches) {
    if (branches == 0) return 0;
    if (branches >= 64) return ~0ULL;
    return (1ULL << branches) - 1ULL;
}

void push_free_slot(int slot) {
    while (!free_slots.push(slot)) std::this_thread::yield();
    free_slots_count.fetch_add(1, std::memory_order_relaxed);
}

bool pop_free_slot(int& slot) {
    if (!free_slots.pop(slot)) return false;
    free_slots_count.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

bool reserve_free_slots(size_t count, std::vector<int>& out) {
    out.clear();
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        int s = -1;
        if (!pop_free_slot(s)) {
            for (int x : out) push_free_slot(x);
            out.clear();
            return false;
        }
        out.push_back(s);
    }
    return true;
}

void push_stage_slot(Stage& stage, int slot) {
    buffer[slot].meta.queue_enter_ns = steady_now_ns();
    if (stage.name == "blur_stage") {
        std::cout << "[push_stage_slot] pushing to " << stage.name << " slot " << slot << std::endl;
    }
    while (!stage.queue.push(slot)) std::this_thread::yield();
    stage.queue_depth.fetch_add(1, std::memory_order_relaxed);
}

bool pop_stage_slot(Stage& stage, int& slot) {
    bool result = stage.queue.pop(slot);
    if (stage.name == "blur_stage" && result) {
        std::cout << "[POP] blur_stage popped slot " << slot << std::endl;
    }
    if (result) {
        stage.queue_depth.fetch_sub(1, std::memory_order_relaxed);
    }
    return result;
}

inline void ema_update(std::atomic<long long>& ema_ns, long long sample_ns) {
    long long old_avg = ema_ns.load(std::memory_order_relaxed);
    long long new_avg = (old_avg == 0) ? sample_ns : ((old_avg * 7) + sample_ns) / 8;
    ema_ns.store(new_avg, std::memory_order_relaxed);
}

Stage* find_stage_by_name(const std::vector<Stage*>& stages, const std::string& name) {
    for (Stage* s : stages) if (s->name == name) return s;
    return nullptr;
}

void free_slot_list(const std::vector<int>& slots) {
    for (int s : slots) if (s >= 0) push_free_slot(s);
}

inline void set_single_output_meta(Frame& f, const std::string& stage_name) {
    f.meta.branch_id = 0;
    f.meta.expected_branches = 1;
    f.meta.source_stage = stage_name.c_str();
}

// ================= REORDER WORKER FOR TERMINAL STAGE =================
void reorder_worker_for_stage(Stage& stage) {
    constexpr size_t REORDER_CAP = 4096;
    struct PendingSlot {
        std::atomic<uint64_t> seq;
        int slot;
        uint64_t frame_id;
        uint64_t created_ns;
    };
    std::cout << "[REORDER] Thread started for stage: " << stage.name << std::endl;
    std::array<PendingSlot, REORDER_CAP> pending;
    for (auto& p : pending) {
        p.seq.store(UINT64_MAX, std::memory_order_relaxed);
        p.slot = -1;
    }
    const uint64_t EMPTY = UINT64_MAX;
    uint64_t next_seq_to_write = 0;

    auto emit = [&](uint64_t seq, int slot, uint64_t created_ns) {
        Frame& frame = buffer[slot];
        if (stage.plugin.api && stage.plugin.api->process) {
            ProcessContext ctx;
            ctx.meta = &frame.meta;
            ctx.stage_name = stage.name.c_str();
            // Вызываем новую версию process
            stage.plugin.api->process(frame.mat, &ctx);
        } else {
            fail_fast_drop("terminal_no_process", frame.meta.frame_id, seq);
        }
        push_free_slot(slot);
        completed_frames.fetch_add(1, std::memory_order_relaxed);
        stage.processed_total.fetch_add(1, std::memory_order_relaxed);
        // Можно добавить метрику времени обработки терминальной стадии
    };

    auto flush_ready = [&]() {
        while (true) {
            size_t idx = next_seq_to_write & (REORDER_CAP - 1);
            uint64_t stored = pending[idx].seq.load(std::memory_order_acquire);
            if (stored != next_seq_to_write) break;
            emit(next_seq_to_write, pending[idx].slot, pending[idx].created_ns);
            pending[idx].seq.store(EMPTY, std::memory_order_release);
            ++next_seq_to_write;
        }
    };

    while (!stage.reorder_stop.load(std::memory_order_relaxed)) {
        OrderedOutputItem item;
        if (!stage.output_queue.pop(item)) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        if (item.global_seq < next_seq_to_write) {
            // устаревший кадр – просто освобождаем слот
            push_free_slot(item.slot);
            continue;
        }
        if (item.global_seq == next_seq_to_write) {
            emit(item.global_seq, item.slot, item.created_ns);
            ++next_seq_to_write;
            flush_ready();
            continue;
        }
        size_t idx = item.global_seq & (REORDER_CAP - 1);
        while (true) {
            uint64_t cur = pending[idx].seq.load(std::memory_order_acquire);
            if (cur == EMPTY || cur == item.global_seq) break;
            std::this_thread::yield();
        }
        pending[idx].slot = item.slot;
        pending[idx].frame_id = item.frame_id;
        pending[idx].created_ns = item.created_ns;
        pending[idx].seq.store(item.global_seq, std::memory_order_release);
        flush_ready();
    }
}

// ================= FORWARD AFTER STAGE =================
void forward_after_stage(Stage& stage, int slot) {
    if (stage.next_stages.empty()) {
        // Терминальная стадия – отправляем в её собственный реордерер
        OrderedOutputItem item;
        item.global_seq = buffer[slot].meta.global_seq;
        item.frame_id = buffer[slot].meta.frame_id;
        item.created_ns = buffer[slot].meta.created_ns;
        item.slot = slot;
        while (!stage.output_queue.push(item)) {
            std::this_thread::yield();
        }
        // слот НЕ освобождается – он будет освобождён реордерером
        return;
    }

    if (stage.next_stages.size() == 1) {
        Stage* next = stage.next_stages[0];
        std::cout << "[FORWARD] " << stage.name << " -> " << next->name 
              << " (next->next_stages.empty()=" << next->next_stages.empty() << ")" << std::endl;
        buffer[slot].meta.source_stage = stage.name.c_str();
        std::cout << "[forward] stage " << stage.name << " forwarding slot " << slot << " to " << next->name << std::endl;
        if (next->next_stages.empty()) {
            // Терминальная стадия – отправляем в её output_queue
            OrderedOutputItem item;
            item.global_seq = buffer[slot].meta.global_seq;
            item.frame_id = buffer[slot].meta.frame_id;
            item.created_ns = buffer[slot].meta.created_ns;
            item.slot = slot;
            while (!next->output_queue.push(item)) {
                std::this_thread::yield();
            }
            // Слот НЕ освобождается – будет освобождён реордерером
        } else {
            std::cout << "[forward] show_stage pushing slot " << slot << " to blur_stage" << std::endl;
            push_stage_slot(*next, slot);
        }
        return;
    }

    const size_t branch_count = stage.next_stages.size();

    std::vector<int> copy_slots;
    if (!reserve_free_slots(branch_count - 1, copy_slots)) {
        dropped_frames.fetch_add(1, std::memory_order_relaxed);
        fail_fast_drop("split/reserve_free_slots_failed",
                       buffer[slot].meta.frame_id,
                       buffer[slot].meta.global_seq);
    }

    const uint64_t split_id = next_split_id();
    buffer[slot].meta.split_id = split_id;
    buffer[slot].meta.branch_id = 0;
    buffer[slot].meta.expected_branches = static_cast<uint16_t>(branch_count);
    buffer[slot].meta.source_stage = stage.name.c_str();

    std::vector<int> out_slots(branch_count, -1);
    out_slots[0] = slot;

    for (size_t i = 1; i < branch_count; ++i) {
        out_slots[i] = copy_slots[i - 1];
        buffer[slot].mat.copyTo(buffer[out_slots[i]].mat);
        buffer[out_slots[i]].meta = buffer[slot].meta;
        buffer[out_slots[i]].meta.branch_id = static_cast<uint16_t>(i);
        buffer[out_slots[i]].meta.expected_branches = static_cast<uint16_t>(branch_count);
        buffer[out_slots[i]].meta.source_stage = stage.name.c_str();
    }

    buffer[slot].meta.branch_id = 0;
    buffer[slot].meta.expected_branches = static_cast<uint16_t>(branch_count);
    buffer[slot].meta.source_stage = stage.name.c_str();

    for (size_t i = 0; i < branch_count; ++i) {
        Stage* next = stage.next_stages[i];
        if (next->next_stages.empty()) {
            OrderedOutputItem item;
            item.global_seq = buffer[out_slots[i]].meta.global_seq;
            item.frame_id = buffer[out_slots[i]].meta.frame_id;
            item.created_ns = buffer[out_slots[i]].meta.created_ns;
            item.slot = out_slots[i];
            while (!next->output_queue.push(item)) {
                std::this_thread::yield();
            }
            // Слот не освобождается – будет освобождён реордерером
        } else {
            push_stage_slot(*next, out_slots[i]);
        }
    }
}

// ================= MERGE HELPER =================
std::vector<int> cleanup_stale_merges(Stage& stage, uint64_t now_ns) {
    std::vector<int> stale_slots;

    std::lock_guard<std::mutex> lock(stage.merge_mutex);
    for (auto it = stage.pending_merges.begin(); it != stage.pending_merges.end();) {
        const auto& g = it->second;
        if (now_ns - g.first_seen_steady_ns > MERGE_TIMEOUT_NS) {
            for (int s : g.slots) if (s >= 0) stale_slots.push_back(s);
            it = stage.pending_merges.erase(it);
        } else {
            ++it;
        }
    }

    return stale_slots;
}

struct MergeIngestResult {
    bool complete = false;
    int base_slot = -1;
    std::vector<int> all_slots;
    std::vector<int> stale_slots;
};

MergeIngestResult ingest_merge_slot(Stage& stage, int slot) {
    MergeIngestResult result;
    const auto& incoming = buffer[slot].meta;
    const uint64_t now = steady_now_ns();

    std::lock_guard<std::mutex> lock(stage.merge_mutex);

    for (auto it = stage.pending_merges.begin(); it != stage.pending_merges.end();) {
        const auto& g = it->second;
        if (now - g.first_seen_steady_ns > MERGE_TIMEOUT_NS) {
            for (int s : g.slots) if (s >= 0) result.stale_slots.push_back(s);
            it = stage.pending_merges.erase(it);
        } else {
            ++it;
        }
    }

    auto& group = stage.pending_merges[incoming.split_id];

    if (group.slots.empty()) {
        group.expected_branches = std::max<uint16_t>(1, incoming.expected_branches);
        group.expected_mask = expected_mask_for(group.expected_branches);
        group.trace_id = incoming.trace_id;
        group.split_id = incoming.split_id;
        group.first_seen_steady_ns = now;
        group.seen_mask = 0;
        group.slots.assign(group.expected_branches, -1);
    }

    if (group.expected_branches != std::max<uint16_t>(1, incoming.expected_branches)) {
        result.stale_slots.push_back(slot);
        return result;
    }

    if (incoming.branch_id >= group.slots.size() || incoming.branch_id >= 64) {
        result.stale_slots.push_back(slot);
        return result;
    }

    if (group.slots[incoming.branch_id] != -1) {
        result.stale_slots.push_back(slot);
        return result;
    }

    group.slots[incoming.branch_id] = slot;
    group.seen_mask |= (1ULL << incoming.branch_id);

    if (group.seen_mask == group.expected_mask) {
        result.complete = true;
        result.base_slot = group.slots[0];
        result.all_slots = group.slots;
        stage.pending_merges.erase(incoming.split_id);
    }

    return result;
}

// ================= JSON LOADING =================
RuntimeConfig load_config(const std::string& path, std::vector<StageSpec>& stages_out) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Cannot open JSON config: " + path);
    }

    json j;
    in >> j;

    RuntimeConfig cfg;

    if (j.contains("frame")) {
        const auto& jf = j.at("frame");
        cfg.frame_h = jf.value("height", cfg.frame_h);
        cfg.frame_w = jf.value("width", cfg.frame_w);
        cfg.frame_channels = jf.value("channels", cfg.frame_channels);
    }

    cfg.buffer_size = j.value("buffer_size", cfg.buffer_size);

    if (j.contains("runtime")) {
        const auto& jr = j.at("runtime");
        cfg.physical_cores = jr.value("physical_cores", cfg.physical_cores);
        cfg.logical_cpus = jr.value("logical_cpus", cfg.logical_cpus);
        cfg.cpu_budget = jr.value("cpu_budget", cfg.cpu_budget);
    }

    if (cfg.logical_cpus <= 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        const int requested_logical = std::max(1, cfg.physical_cores) * 2;
        cfg.logical_cpus = hw ? std::min(requested_logical, static_cast<int>(hw)) : requested_logical;
    }

    if (cfg.cpu_budget <= 0) {
        cfg.cpu_budget = std::max(1, cfg.logical_cpus - 1);
    }

    cfg.cpu_budget = std::min(cfg.cpu_budget, cfg.logical_cpus);

    if (cfg.buffer_size <= 0) {
        throw std::runtime_error("buffer_size must be > 0");
    }
    if (cfg.buffer_size >= static_cast<int>(FREE_SLOTS_CAP)) {
        throw std::runtime_error("buffer_size must be smaller than FREE_SLOTS_CAP");
    }
    if (cfg.frame_h <= 0 || cfg.frame_w <= 0) {
        throw std::runtime_error("frame size must be positive");
    }
    if (cfg.frame_channels <= 0 || cfg.frame_channels > 4) {
        throw std::runtime_error("frame.channels must be in [1..4]");
    }

    cfg.cpu_ids.reserve(cfg.logical_cpus);
    for (int i = 0; i < cfg.logical_cpus; ++i) {
        cfg.cpu_ids.push_back(i);
    }

    if (j.contains("ipconfig")) {
        const auto& jip = j.at("ipconfig");
        cfg.ip = jip.value("ip", cfg.ip);
        cfg.port = jip.value("port", cfg.port);
        cfg.protocol = jip.value("protocol", cfg.protocol);
        cfg.socket_type = jip.value("socket_type", cfg.socket_type);
    }

    if (!j.contains("stages") || !j.at("stages").is_array()) {
        throw std::runtime_error("JSON must contain array field 'stages'");
    }

    for (const auto& js : j.at("stages")) {
        StageSpec s;
        s.name = js.at("name").get<std::string>();
        s.callable = js.at("callable").get<std::string>();
        s.plugin_path = js.value("plugin", std::string{});

        if (js.contains("next")) {
            const auto& next_val = js.at("next");
            if (next_val.is_array()) {
                if (next_val.size() > 64) {
                    throw std::runtime_error("Stage '" + s.name + "' has more than 64 next stages");
                }
                for (const auto& item : next_val) {
                    std::string next_name = item.get<std::string>();
                    if (!next_name.empty()) {
                        s.next.push_back(next_name);
                    }
                }
            } else if (next_val.is_string()) {
                std::string next_str = next_val.get<std::string>();
                if (!next_str.empty()) {
                    s.next.push_back(next_str);
                }
            }
        }

        s.initial_workers = js.value("initial_workers", 1);
        s.min_workers = js.value("min_workers", 1);
        s.max_workers = js.value("max_workers", 0);
        s.queue_threshold = js.value("queue_threshold", 8);
        s.time_threshold_ns = js.value("time_threshold_ns", 5'000'000LL);

        if (s.initial_workers < 0) s.initial_workers = 0;
        if (s.min_workers < 0) s.min_workers = 0;
        if (s.max_workers < 0) s.max_workers = 0;
        if (s.queue_threshold < 0) s.queue_threshold = 0;
        if (s.time_threshold_ns < 0) s.time_threshold_ns = 0;

        stages_out.push_back(std::move(s));
    }

    if (stages_out.empty()) {
        throw std::runtime_error("JSON 'stages' must not be empty");
    }

    return cfg;
}

// ================= PIPELINE BUILD =================
struct Pipeline {
    std::vector<std::unique_ptr<Stage>> storage;
    std::vector<Stage*> ordered;
};

static std::string default_plugin_path(const StageSpec& spec, const std::string&) {
    return "./plugins/" + spec.callable + "_plugin.so";
}

Pipeline build_pipeline(const RuntimeConfig& cfg, const std::vector<StageSpec>& specs) {
    Pipeline p;
    p.storage.reserve(specs.size());
    p.ordered.reserve(specs.size());

    for (const auto& spec : specs) {
        auto st = std::make_unique<Stage>();
        st->name = spec.name;
        st->callable = spec.callable;
        st->plugin_path = spec.plugin_path;
        st->min_workers = std::max(0, spec.min_workers);
        st->initial_workers = std::max(0, spec.initial_workers);
        st->max_workers = (spec.max_workers > 0) ? spec.max_workers : cfg.cpu_budget;
        st->queue_threshold = spec.queue_threshold;
        st->time_threshold_ns = spec.time_threshold_ns;

        if (st->max_workers <= 0) {
            st->max_workers = cfg.cpu_budget;
        }
        if (st->max_workers > cfg.cpu_budget) {
            st->max_workers = cfg.cpu_budget;
        }
        if (st->initial_workers < st->min_workers) {
            st->initial_workers = st->min_workers;
        }
        if (st->initial_workers > st->max_workers) {
            st->initial_workers = st->max_workers;
        }

        Stage* raw = st.get();
        p.ordered.push_back(raw);
        p.storage.push_back(std::move(st));
    }

    for (size_t i = 0; i < specs.size(); ++i) {
        Stage* current = p.ordered[i];
        const auto& spec = specs[i];

        if (!spec.next.empty()) {
            for (const auto& next_name : spec.next) {
                Stage* next = find_stage_by_name(p.ordered, next_name);
                if (!next) {
                    throw std::runtime_error("Stage '" + spec.name + "' next target not found: " + next_name);
                }
                current->next_stages.push_back(next);
            }
        } else if (i + 1 < specs.size()) {
            current->next_stages.push_back(p.ordered[i + 1]);
        }
    }

    std::unordered_map<Stage*, int> indegree;
    for (Stage* s : p.ordered) indegree[s] = 0;
    for (Stage* s : p.ordered) {
        for (Stage* nxt : s->next_stages) {
            indegree[nxt]++;
        }
    }

    for (Stage* s : p.ordered) {
        s->kind = (indegree[s] >= 2) ? "merge" : "normal";

        if (s->plugin_path.empty()) {
            s->plugin_path = default_plugin_path(StageSpec{ s->name, s->callable, {}, {}, 1, 1, 0, 8, 5'000'000LL }, s->kind);
        } else if (std::filesystem::is_directory(s->plugin_path)) {
            throw std::runtime_error("Stage '" + s->name + "': plugin path points to a directory: " + s->plugin_path);
        } else if (s->plugin_path == "./plugins/" || s->plugin_path == ".") {
            s->plugin_path = default_plugin_path(StageSpec{ s->name, s->callable, {}, {}, 1, 1, 0, 8, 5'000'000LL }, s->kind);
        }

        if (s->kind == "normal") {
            if (s->plugin_path.empty()) {
                s->plugin_path = default_plugin_path(StageSpec{ s->name, s->callable, {}, {}, 1, 1, 0, 8, 5'000'000LL }, s->kind);
            }
            s->plugin = load_plugin(s->plugin_path, true, false);
        } else {
            if (s->plugin_path.empty()) {
                s->plugin_path = default_plugin_path(StageSpec{ s->name, s->callable, {}, {}, 1, 1, 0, 8, 5'000'000LL }, s->kind);
            }
            s->plugin = load_plugin(s->plugin_path, false, true);
        }
    }

    return p;
}

// ================= METRICS EXPORT =================
void metrics_server_loop(const std::string& csv_filename) {
    std::ofstream file(csv_filename, std::ios::out);
    if (!file.is_open()) {
        std::cerr << "[METRICS] Cannot open " << csv_filename << " for writing\n";
        return;
    }

    file.seekp(0, std::ios::end);
    if (file.tellp() == 0) {
        file << "timestamp,input_fps,output_fps,completed_frames,free_slots,free_slots_min,"
                "dropped_frames,reorder_queue_depth,"
                "e2e_avg_ms,e2e_p50_ms,e2e_p95_ms,e2e_p99_ms,e2e_max_ms,e2e_tail10_pct,e2e_tail50_pct";
        for (const Stage* s : g_pipeline_stages) {
            file << "," << s->name << "_processed"
                 << "," << s->name << "_interval_share_pct"
                 << "," << s->name << "_proc_avg_ms"
                 << "," << s->name << "_queue_avg_ms"
                 << "," << s->name << "_queue_depth"
                 << "," << s->name << "_fps"
                 << "," << s->name << "_util_pct";
        }
        file << "\n";
    }

    using clock = std::chrono::steady_clock;
    auto last_time = clock::now();
    long long last_input = 0;
    long long last_completed = 0;

    struct StageSnapshot {
        long long processed = 0;
        long long time_ns = 0;
        double proc_avg_ms = 0.0;
        double queue_avg_ms = 0.0;
    };
    std::vector<StageSnapshot> last_stage_snapshot(g_pipeline_stages.size());

    long long last_free_slots = free_slots_count.load(std::memory_order_relaxed);
    long long free_slots_min_this_interval = last_free_slots;

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        const auto now = clock::now();
        const double elapsed = std::chrono::duration<double>(now - last_time).count();

        // --- глобальные счётчики ---
        const long long current_input = input_frames.load(std::memory_order_relaxed);
        const long long current_completed = completed_frames.load(std::memory_order_relaxed);
        const double input_fps = (elapsed > 0) ? ((current_input - last_input) / elapsed) : 0.0;
        const double output_fps = (elapsed > 0) ? ((current_completed - last_completed) / elapsed) : 0.0;

        // --- свободные слоты: текущее значение и минимум за интервал ---
        long long current_free_slots = free_slots_count.load(std::memory_order_relaxed);
        if (current_free_slots < free_slots_min_this_interval)
            free_slots_min_this_interval = current_free_slots;
        const long long free_slots_min = free_slots_min_this_interval;
        free_slots_min_this_interval = current_free_slots;   // сброс для следующего интервала

        // --- e2e latency ---
        std::vector<uint64_t> e2e_samples;
        g_e2e_latency_ns.snapshot(e2e_samples);
        LatencyStats e2e_stats = summarize_latency(std::move(e2e_samples));

        // --- собираем данные по стадиям ---
        std::vector<StageSnapshot> current_stage_snapshot(g_pipeline_stages.size());
        long long interval_stage_time_sum_ns = 0;

        for (size_t i = 0; i < g_pipeline_stages.size(); ++i) {
            Stage* s = g_pipeline_stages[i];
            StageSnapshot snap;
            snap.processed = s->processed_total.load(std::memory_order_relaxed);
            snap.time_ns = s->time_ns_total.load(std::memory_order_relaxed);

            // Время обработки (среднее) за интервал
            std::vector<uint64_t> proc_samples, queue_samples;
            s->process_latency_ns.snapshot(proc_samples);
            s->queue_wait_latency_ns.snapshot(queue_samples);

            if (!proc_samples.empty()) {
                double sum = std::accumulate(proc_samples.begin(), proc_samples.end(), 0.0);
                snap.proc_avg_ms = (sum / proc_samples.size()) / 1e6;
            } else {
                snap.proc_avg_ms = 0.0;
            }
            if (!queue_samples.empty()) {
                double sum = std::accumulate(queue_samples.begin(), queue_samples.end(), 0.0);
                snap.queue_avg_ms = (sum / queue_samples.size()) / 1e6;
            } else {
                snap.queue_avg_ms = 0.0;
            }

            current_stage_snapshot[i] = snap;

            const long long delta_time = snap.time_ns - last_stage_snapshot[i].time_ns;
            if (delta_time > 0) interval_stage_time_sum_ns += delta_time;
        }

        // --- вывод в консоль (режим quiet / normal) ---
        if (g_quiet) {
            std::cout << "[STATS] Input FPS: " << std::fixed << std::setprecision(2) << input_fps
                      << " | Output FPS: " << output_fps << "\n";
        } else {
            std::cout << "[STATS] Input FPS: " << std::fixed << std::setprecision(2) << input_fps
                      << " | Output FPS: " << output_fps
                      << " | Free slots: " << current_free_slots
                      << " | Dropped: " << dropped_frames.load(std::memory_order_relaxed)
                      << " | ReorderQ: 0 (per-stage buffers)"   // старый глобальный удалён
                      << " | Workers:";
            for (const Stage* s : g_pipeline_stages) {
                std::cout << " " << s->name << "=" << s->worker_count.load(std::memory_order_relaxed);
            }
            std::cout << "\n";
        }

        // --- запись в CSV ---
        const auto ts = std::chrono::system_clock::now().time_since_epoch().count();
        file << ts << "," << std::fixed << std::setprecision(2)
             << input_fps << "," << output_fps << ","
             << current_completed << ","
             << current_free_slots << "," << free_slots_min << ","
             << dropped_frames.load(std::memory_order_relaxed) << ","
             << 0  // старый reorder_queue_count удалён
             << "," << std::setprecision(3)
             << e2e_stats.avg_ms << "," << e2e_stats.p50_ms << "," << e2e_stats.p95_ms << ","
             << e2e_stats.p99_ms << "," << e2e_stats.max_ms << ","
             << e2e_stats.tail_gt_10ms_pct << "," << e2e_stats.tail_gt_50ms_pct;

        for (size_t i = 0; i < g_pipeline_stages.size(); ++i) {
            Stage* s = g_pipeline_stages[i];
            const StageSnapshot& snap = current_stage_snapshot[i];
            const StageSnapshot& prev = last_stage_snapshot[i];

            const long long processed_delta = snap.processed - prev.processed;
            const double stage_fps = (elapsed > 0) ? (processed_delta / elapsed) : 0.0;

            const long long delta_time_ns = snap.time_ns - prev.time_ns;
            const double share_pct = (interval_stage_time_sum_ns > 0)
                ? (100.0 * static_cast<double>(delta_time_ns) / static_cast<double>(interval_stage_time_sum_ns))
                : 0.0;

            const int workers = s->worker_count.load(std::memory_order_relaxed);
            double util_pct = 0.0;
            if (workers > 0 && snap.proc_avg_ms > 0.0) {
                // utilisation = (stage_fps * avg_proc_time_ms) / (workers * 1000) * 100%
                util_pct = (stage_fps * snap.proc_avg_ms) / (workers * 10.0);
                if (util_pct > 100.0) util_pct = 100.0;
            }

            const int queue_depth = s->queue_depth.load(std::memory_order_relaxed);

            file << "," << snap.processed
                 << "," << std::setprecision(3) << share_pct
                 << "," << snap.proc_avg_ms
                 << "," << snap.queue_avg_ms
                 << "," << queue_depth
                 << "," << stage_fps
                 << "," << util_pct;
        }

        file << "\n";
        file.flush();

        // --- обновление "прошлых" значений ---
        last_input = current_input;
        last_completed = current_completed;
        last_time = now;
        last_stage_snapshot.swap(current_stage_snapshot);
    }
}

// ================= WORKERS =================
void worker_loop(Stage& stage, int cpu_id) {

    pin_current_thread_to_cpu(cpu_id);

    const bool has_process = (stage.plugin.api && stage.plugin.api->process);
    const bool has_merge = (stage.plugin.api && stage.plugin.api->merge);

    if (stage.kind == "normal" && !has_process) {
        std::cerr << "[FATAL] stage process() is null for stage: " << stage.name.c_str() << "\n";
        return;
    }
    if (stage.kind == "merge" && !has_merge) {
        std::cerr << "[FATAL] stage merge() is null for stage: " << stage.name.c_str() << "\n";
        return;
    }

    while (true) {
        int slot = -1;

        if (!pop_stage_slot(stage, slot)) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        if (stage.name == "show_stage") {
            static int show_calls = 0;
            if (++show_calls % 100 == 0) {
                std::cout << "[worker_loop] show_stage processing slot " << slot << std::endl;
            }
        }
        if (stage.name == "blur_stage") {
            static int blur_calls = 0;
            if (++blur_calls % 100 == 0) {
                std::cout << "[worker_loop] blur_stage processing slot " << slot << std::endl;
            }
        }
        const uint64_t dequeue_ns = steady_now_ns();
        const uint64_t queue_enter_ns = buffer[slot].meta.queue_enter_ns;
        if (queue_enter_ns > 0 && dequeue_ns >= queue_enter_ns) {
            stage.queue_wait_latency_ns.add(dequeue_ns - queue_enter_ns);
        }

        const auto start = std::chrono::high_resolution_clock::now();

        if (stage.kind == "merge") {
            const auto incoming_meta = buffer[slot].meta;

            if (incoming_meta.expected_branches <= 1) {
                if (has_process) {
                    ProcessContext ctx;
                    ctx.meta = &buffer[slot].meta;
                    ctx.stage_name = stage.name.c_str();
                    stage.plugin.api->process(buffer[slot].mat, &ctx);
                }

                stage.processed_total.fetch_add(1, std::memory_order_relaxed);

                const auto end = std::chrono::high_resolution_clock::now();
                const auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                stage.time_ns_total.fetch_add(dt, std::memory_order_relaxed);
                stage.process_latency_ns.add(static_cast<uint64_t>((dt > 0) ? dt : 0));
                ema_update(stage.ema_time_ns, dt);

                forward_after_stage(stage, slot);
                continue;
            }

            MergeIngestResult res = ingest_merge_slot(stage, slot);

            if (!res.stale_slots.empty()) {
                free_slot_list(res.stale_slots);
                dropped_frames.fetch_add(1, std::memory_order_relaxed);
                fail_fast_drop("merge/stale_or_timeout_branch",
                               buffer[slot].meta.frame_id,
                               buffer[slot].meta.global_seq);
            }

            stage.processed_total.fetch_add(1, std::memory_order_relaxed);

            if (!res.complete) {
                const auto end = std::chrono::high_resolution_clock::now();
                const auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                stage.time_ns_total.fetch_add(dt, std::memory_order_relaxed);
                stage.process_latency_ns.add(static_cast<uint64_t>((dt > 0) ? dt : 0));
                ema_update(stage.ema_time_ns, dt);
                continue;
            }

            if (res.base_slot < 0) {
                for (int s : res.all_slots) {
                    if (s >= 0) push_free_slot(s);
                }
                dropped_frames.fetch_add(1, std::memory_order_relaxed);
                fail_fast_drop("merge/base_slot_missing",
                               buffer[slot].meta.frame_id,
                               buffer[slot].meta.global_seq);
            }

            std::vector<const cv::Mat*> inputs;
            inputs.reserve(res.all_slots.size());
            for (int s : res.all_slots) {
                if (s >= 0) inputs.push_back(&buffer[s].mat);
            }

            if (!inputs.empty()) {
                stage.plugin.api->merge(buffer[res.base_slot].mat, inputs.data(), inputs.size());
            }

            for (int s : res.all_slots) {
                if (s >= 0 && s != res.base_slot) {
                    push_free_slot(s);
                }
            }

            buffer[res.base_slot].meta.branch_id = 0;
            buffer[res.base_slot].meta.expected_branches = 1;
            buffer[res.base_slot].meta.source_stage = stage.name.c_str();

            const auto end = std::chrono::high_resolution_clock::now();
            const auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            stage.time_ns_total.fetch_add(dt, std::memory_order_relaxed);
            stage.process_latency_ns.add(static_cast<uint64_t>((dt > 0) ? dt : 0));
            ema_update(stage.ema_time_ns, dt);

            forward_after_stage(stage, res.base_slot);
            continue;
        }

        ProcessContext ctx;
        ctx.meta = &buffer[slot].meta;
        ctx.stage_name = stage.name.c_str();
        stage.plugin.api->process(buffer[slot].mat, &ctx);

        stage.processed_total.fetch_add(1, std::memory_order_relaxed);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        stage.time_ns_total.fetch_add(dt, std::memory_order_relaxed);
        stage.process_latency_ns.add(static_cast<uint64_t>((dt > 0) ? dt : 0));
        ema_update(stage.ema_time_ns, dt);

        forward_after_stage(stage, slot);
    }
}

bool spawn_worker(Stage& stage) {
    if (stage.worker_count.load(std::memory_order_relaxed) >= stage.max_workers) return false;
    if (g_total_runtime_workers.load(std::memory_order_relaxed) >= g_worker_budget) return false;

    const int cpu_id = next_cpu_id();

    try {
        std::thread(worker_loop, std::ref(stage), cpu_id).detach();
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Failed to spawn worker for stage " << stage.name.c_str() << ": " << e.what() << "\n";
        return false;
    }

    stage.worker_count.fetch_add(1, std::memory_order_relaxed);
    g_total_runtime_workers.fetch_add(1, std::memory_order_relaxed);

    std::cout << "[AUTO-SCALE] " << stage.name.c_str()
              << " -> workers=" << stage.worker_count.load(std::memory_order_relaxed) << "\n";
    return true;
}

void start_initial_workers(Stage& stage) {
    const int target = stage.initial_workers;
    for (int i = 0; i < target; ++i) {
        if (!spawn_worker(stage)) break;
    }
}

// ================= AUTOSCALER =================
void autoscaler_loop(std::vector<Stage*> stages) {
    using clock = std::chrono::steady_clock;

    constexpr int SAMPLE_MS = 50;
    constexpr int DECISION_TICKS = 4;
    constexpr int PANIC_QUEUE_THRESHOLD = 20;

    const std::chrono::milliseconds MIN_SCALE_INTERVAL(200);
    auto last_scale_time = clock::now();

    struct StageState {
        int overload_ticks = 0;
        int last_queue = 0;
    };

    std::vector<StageState> state(stages.size());

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(SAMPLE_MS));
        auto now = clock::now();

        for (Stage* s : stages) {
            if (s->kind == "merge") {
                auto stale = cleanup_stale_merges(*s, steady_now_ns());
                if (!stale.empty()) {
                    free_slot_list(stale);
                    dropped_frames.fetch_add(1, std::memory_order_relaxed);
                    fail_fast_drop("autoscaler/merge_timeout", 0, 0);
                }
            }
        }

        Stage* best_stage = nullptr;
        double best_pressure = 0.0;

        for (size_t i = 0; i < stages.size(); ++i) {
            Stage* s = stages[i];

            int workers = s->worker_count.load(std::memory_order_relaxed);
            if (workers <= 0) continue;

            int q = s->queue_depth.load(std::memory_order_relaxed);
            long long ema = s->ema_time_ns.load(std::memory_order_relaxed);

            double pressure = (double)q * (double)ema / (double)workers;

            int prev_q = state[i].last_queue;
            bool growing = (q > prev_q);

            state[i].last_queue = q;

            bool overloaded = (q > workers * 2) && (ema > 0);
            if (overloaded && growing) state[i].overload_ticks++;
            else state[i].overload_ticks = std::max(0, state[i].overload_ticks - 1);

            bool panic = (q > PANIC_QUEUE_THRESHOLD) && growing;

            if (workers < s->max_workers) {
                if (panic) {
                    best_stage = s;
                    break;
                }
                if (state[i].overload_ticks >= DECISION_TICKS) {
                    if (pressure > best_pressure) {
                        best_pressure = pressure;
                        best_stage = s;
                    }
                }
            }
        }

        if (best_stage) {
            if (now - last_scale_time >= MIN_SCALE_INTERVAL) {
                if (spawn_worker(*best_stage)) {
                    last_scale_time = now;
                }
            }
        }
    }
}

// ================= MAIN =================
int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <config.json> [--quiet|-q]\n";
            return 1;
        }

        const std::string config_path = argv[1];

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--quiet" || arg == "-q") {
                g_quiet = true;
                break;
            }
        }

        std::filesystem::create_directory("results");
        std::filesystem::path config_file(config_path);
        std::string base_name = config_file.stem().string();

        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ts;
        ts << std::put_time(std::localtime(&in_time_t), "_%Y%m%d_%H%M%S");
        std::string metrics_path = "results/" + base_name + ts.str() + ".csv";

        std::vector<StageSpec> stage_specs;
        g_cfg = load_config(config_path, stage_specs);

        cv::setNumThreads(1);

        buffer.resize(g_cfg.buffer_size);
        for (auto& f : buffer) {
            f.meta = {};
            f.mat.create(
                g_cfg.frame_h,
                g_cfg.frame_w,
                CV_MAKETYPE(CV_8U, g_cfg.frame_channels)
            );
        }

        g_worker_budget = std::max(1, g_cfg.cpu_budget);

        Pipeline pipeline = build_pipeline(g_cfg, stage_specs);
        g_pipeline_stages = pipeline.ordered;

        free_slots_count.store(g_cfg.buffer_size, std::memory_order_relaxed);
        for (int i = 0; i < g_cfg.buffer_size; ++i) {
            while (!free_slots.push(i)) std::this_thread::yield();
        }

        for (Stage* s : g_pipeline_stages) {
            if (s->next_stages.empty()) {
                std::cout << "[MAIN] Starting reorder thread for terminal stage: " << s->name << std::endl;
                s->reorder_thread = std::make_unique<std::thread>(reorder_worker_for_stage, std::ref(*s));
            }else {
                start_initial_workers(*s);
            }
        }

        std::thread(autoscaler_loop, g_pipeline_stages).detach();
        std::thread(metrics_server_loop, metrics_path).detach();

        std::cout << "Loaded pipeline from: " << config_path << "\n";
        std::cout << "Metrics file: " << metrics_path << "\n";
        std::cout << "Frame: " << g_cfg.frame_h << " x " << g_cfg.frame_w
                  << " x " << g_cfg.frame_channels << "\n";
        std::cout << "Buffer size: " << g_cfg.buffer_size << "\n";
        std::cout << "Logical CPUs: " << g_cfg.logical_cpus << "\n";
        std::cout << "CPU budget for workers: " << g_worker_budget << "\n";

        if (!g_quiet) {
            for (size_t i = 0; i < g_pipeline_stages.size(); ++i) {
                const Stage* s = g_pipeline_stages[i];
                std::cout << "Stage " << i << ": " << s->name
                          << " | kind=" << s->kind
                          << " | plugin=" << s->plugin_path
                          << " | initial=" << s->initial_workers
                          << " | min=" << s->min_workers
                          << " | max=" << s->max_workers;

                if (!s->next_stages.empty()) {
                    std::cout << " | next=[";
                    for (size_t j = 0; j < s->next_stages.size(); ++j) {
                        if (j > 0) std::cout << ", ";
                        std::cout << s->next_stages[j]->name;
                    }
                    std::cout << "]";
                } else {
                    std::cout << " | next=[] (terminal)";
                }
                std::cout << "\n";
            }
        }

        pin_current_thread_to_cpu(g_cfg.cpu_ids.empty() ? 0 : g_cfg.cpu_ids.front());

        zmq::context_t ctx(1);
        zmq::socket_t sock(ctx, zmq::socket_type::pull);
        std::string connect_addr = g_cfg.protocol + "://" + g_cfg.ip + ":" + std::to_string(g_cfg.port);
        sock.connect(connect_addr);
        std::cout << "Connecting to ZMQ source at " << connect_addr << "\n";

        std::cout << "Expected frame size: "
                  << g_cfg.frame_h << " x " << g_cfg.frame_w
                  << " x " << g_cfg.frame_channels << "\n";
        std::cout << "Waiting for frames...\n";

        while (true) {
            zmq::message_t msg;
            if (!sock.recv(msg, zmq::recv_flags::none)) {
                continue;
            }

            if (msg.size() % sizeof(capnp::word) != 0) {
                std::cerr << "[WARN] Bad Cap'n Proto message size: " << msg.size() << "\n";
                continue;
            }

            std::vector<capnp::word> words(msg.size() / sizeof(capnp::word));
            std::memcpy(words.data(), msg.data(), msg.size());

            capnp::FlatArrayMessageReader reader(
                kj::ArrayPtr<const capnp::word>(words.data(), words.size())
            );
            FrameEnvelope::Reader env = reader.getRoot<FrameEnvelope>();

            const uint32_t h = env.getHeight();
            const uint32_t w = env.getWidth();
            const uint16_t c = env.getChannels();

            if (h != static_cast<uint32_t>(g_cfg.frame_h) ||
                w != static_cast<uint32_t>(g_cfg.frame_w) ||
                c != static_cast<uint16_t>(g_cfg.frame_channels)) {
                std::cerr << "[WARN] Frame dims mismatch: got "
                          << h << "x" << w << "x" << c
                          << ", expected "
                          << g_cfg.frame_h << "x" << g_cfg.frame_w << "x" << g_cfg.frame_channels
                          << "\n";
                continue;
            }

            const size_t expected_bytes = static_cast<size_t>(h) * static_cast<size_t>(w) * static_cast<size_t>(c);

            if (env.getPixels().size() != expected_bytes) {
                std::cerr << "[WARN] Wrong payload size: got " << env.getPixels().size()
                          << ", expected " << expected_bytes << "\n";
                continue;
            }

            int slot = -1;
            if (!free_slots.pop(slot)) {
                dropped_frames.fetch_add(1, std::memory_order_relaxed);
                fail_fast_drop("input/free_slots_empty", 0, g_global_seq_counter.load(std::memory_order_relaxed));
            }
            free_slots_count.fetch_sub(1, std::memory_order_relaxed);

            const auto meta = env.getMeta();
            buffer[slot].meta.frame_id = meta.getFrameId();
            buffer[slot].meta.trace_id = meta.getTraceId();
            buffer[slot].meta.split_id = meta.getSplitId();
            buffer[slot].meta.branch_id = meta.getBranchId();
            buffer[slot].meta.expected_branches = meta.getExpectedBranches();
            buffer[slot].meta.created_ns = steady_now_ns();
            buffer[slot].meta.source_stage = meta.getSourceStage().cStr();
            buffer[slot].meta.global_seq = g_global_seq_counter.fetch_add(1, std::memory_order_relaxed);

            if (buffer[slot].meta.split_id == 0) {
                buffer[slot].meta.split_id = buffer[slot].meta.trace_id;
            }

            std::memcpy(buffer[slot].mat.data, env.getPixels().begin(), expected_bytes);

            input_frames.fetch_add(1, std::memory_order_relaxed);
            push_stage_slot(*g_pipeline_stages.front(), slot);
            std::cout << "[MAIN] Pushed slot " << slot << " to first stage: " << g_pipeline_stages.front()->name << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }
}