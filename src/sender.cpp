#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <cstring>
#include <cstdint>
#include <atomic>

#include <zmq.hpp>
#include <capnp/serialize.h>
#include <capnp/message.h>
#include <kj/array.h>

#include <H5Cpp.h>

#include "frame.capnp.h"

using namespace H5;

static uint64_t system_now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

static bool send_frame(zmq::socket_t& socket,
                       uint64_t frame_id,
                       uint64_t trace_id,
                       uint64_t split_id,
                       uint16_t branch_id,
                       uint16_t expected_branches,
                       uint32_t height,
                       uint32_t width,
                       uint16_t channels,
                       const uint8_t* pixels,
                       size_t pixel_bytes)
{
    capnp::MallocMessageBuilder message;
    FrameEnvelope::Builder env = message.initRoot<FrameEnvelope>();

    auto meta = env.initMeta();
    meta.setFrameId(frame_id);
    meta.setTraceId(trace_id);
    meta.setSplitId(split_id);
    meta.setBranchId(branch_id);
    meta.setExpectedBranches(expected_branches);
    meta.setSourceStage("source");
    meta.setCreatedNs(system_now_ns());

    env.setHeight(height);
    env.setWidth(width);
    env.setChannels(channels);

    auto blob = env.initPixels(pixel_bytes);
    std::memcpy(blob.begin(), pixels, pixel_bytes);

    kj::Array<capnp::word> flat = capnp::messageToFlatArray(message);
    auto bytes = flat.asBytes();

    zmq::message_t out(bytes.size());
    std::memcpy(out.data(), bytes.begin(), bytes.size());
    auto res = socket.send(out, zmq::send_flags::dontwait);
    return res.has_value();
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <h5_file> [sleep_ms]\n";
            std::cerr << "  h5_file   - path to HDF5 file containing frames\n";
            std::cerr << "  sleep_ms  - milliseconds between frames (default: 25)\n";
            return 1;
        }

        const std::string h5_path = argv[1];
        int sleep_ms = 25;
        if (argc >= 3) {
            sleep_ms = std::stoi(argv[2]);
            if (sleep_ms <= 0) sleep_ms = 1;
        }

        const std::string dataset_name = "data";

        H5File file(h5_path, H5F_ACC_RDONLY);
        DataSet ds = file.openDataSet(dataset_name);
        DataSpace space = ds.getSpace();

        const int rank = space.getSimpleExtentNdims();
        if (rank != 3 && rank != 4) {
            throw std::runtime_error("Expected dataset rank 3 or 4: [N,H,W] or [N,H,W,C]");
        }

        std::vector<hsize_t> dims(rank);
        space.getSimpleExtentDims(dims.data());

        const hsize_t num_frames = dims[0];
        const uint32_t height = static_cast<uint32_t>(dims[1]);
        const uint32_t width  = static_cast<uint32_t>(dims[2]);
        const uint16_t channels = (rank == 4) ? static_cast<uint16_t>(dims[3]) : 1;
        const size_t pixel_bytes = static_cast<size_t>(height) * width * channels;

        std::cout << "Loading " << num_frames << " frames from " << h5_path << "...\n";
        std::vector<std::vector<uint8_t>> frames(num_frames);
        for (hsize_t i = 0; i < num_frames; ++i) {
            frames[i].resize(pixel_bytes);
            DataSpace file_space = ds.getSpace();
            if (rank == 3) {
                hsize_t start[3] = { i, 0, 0 };
                hsize_t count[3] = { 1, height, width };
                file_space.selectHyperslab(H5S_SELECT_SET, count, start);
                hsize_t mem_dims[2] = { height, width };
                DataSpace mem_space(2, mem_dims);
                ds.read(frames[i].data(), PredType::NATIVE_UCHAR, mem_space, file_space);
            } else {
                hsize_t start[4] = { i, 0, 0, 0 };
                hsize_t count[4] = { 1, height, width, channels };
                file_space.selectHyperslab(H5S_SELECT_SET, count, start);
                hsize_t mem_dims[3] = { height, width, channels };
                DataSpace mem_space(3, mem_dims);
                ds.read(frames[i].data(), PredType::NATIVE_UCHAR, mem_space, file_space);
            }
            if ((i + 1) % (num_frames / 10) == 0 || i + 1 == num_frames) {
                std::cout << "Loaded " << (i + 1) << "/" << num_frames << " frames\r" << std::flush;
            }
        }
        std::cout << "\nLoaded " << num_frames << " frames.\n";

        zmq::context_t ctx(1);
        zmq::socket_t socket(ctx, zmq::socket_type::push);
        socket.set(zmq::sockopt::sndhwm, 10000);
        socket.set(zmq::sockopt::sndbuf, 50 * 1024 * 1024);
        socket.bind("tcp://127.0.0.1:5558");

        uint64_t i = 0;
        using clock = std::chrono::steady_clock;
        auto next_time = clock::now();

        uint64_t total_sent = 0;
        uint64_t total_dropped = 0;

        while (true) {
            bool sent = send_frame(socket,
                                   i, i, i, 0, 1,
                                   height, width, channels,
                                   frames[i].data(), frames[i].size());

            if (sent) {
                total_sent++;
            } else {
                total_dropped++;
                if (total_dropped % 100 == 1) {
                    std::cerr << "\n! ZMQ send failed (total dropped: " << total_dropped << ")\n" << std::flush;
                }
            }

            i = (i + 1) % num_frames;
            next_time += std::chrono::milliseconds(sleep_ms);
            std::this_thread::sleep_until(next_time);
        }
    } catch (const std::exception& e) {
        std::cerr << "\n[FATAL] " << e.what() << "\n";
        return 1;
    }
}