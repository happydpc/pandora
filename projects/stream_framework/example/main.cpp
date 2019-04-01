#include "stream/task_pool.h"
#include "stream/transform_task.h"
#include <future>
#include <iostream>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/spdlog.h>
#include <tbb/tbb.h>
#include <thread>

void testGraphBuilder();

static constexpr int stepSize = 1024;
static constexpr int streamSize = 8 * 1024 * 1024;

void datastreamTestSingleThreaded()
{
    tasking::DataStream<int> stream { static_cast<size_t>(streamSize / 8) };
    for (int i = 0; i < streamSize; i += stepSize) {
        auto write = stream.reserveRangeForWriting(stepSize);
        for (int j = 0, k = i; j < stepSize; j++, k++)
            write.data[j] = k;
    }

    int sum = 0;
    while (auto dataChunkOpt = stream.popChunk()) {
        for (int v : dataChunkOpt->getData()) {
            if ((v % 2) == 0)
                sum += v;
            else
                sum -= v;
        }
    }

    assert(sum == -4194304);
}

void dataStreamTestMultiThreadPush()
{
    tasking::DataStream<int> stream { static_cast<size_t>(streamSize / 8) };

    tbb::blocked_range<int> range = tbb::blocked_range<int>(0, streamSize, stepSize);
    tbb::parallel_for(range, [&](tbb::blocked_range<int> localRange) {
        auto write = stream.reserveRangeForWriting(localRange.size());
        for (int i = 0, j = localRange.begin(); i < localRange.size(); i++, j++)
            write.data[i] = j;
    });

    int sum = 0;
    while (auto dataChunkOpt = stream.popChunk()) {
        for (int v : dataChunkOpt->getData()) {
            if ((v % 2) == 0)
                sum += v;
            else
                sum -= v;
        }
    }

    assert(sum == -4194304);
}

void dataStreamTestProducerConsumer()
{
    tasking::DataStream<int> stream { static_cast<size_t>(streamSize / 8) };
    std::thread producer([&]() {
        for (int i = 0; i < streamSize; i += stepSize) {
            auto write = stream.reserveRangeForWriting(stepSize);
            for (int j = 0, k = i; j < stepSize; j++, k++)
                write.data[j] = k;
        }
    });

    std::promise<int> sumPromise;
    std::thread consumer([&]() {
        int sum = 0;
        while (sum != -4194304) {
            while (auto dataChunkOpt = stream.popChunk()) {
                for (int v : dataChunkOpt->getData()) {
                    if ((v % 2) == 0)
                        sum += v;
                    else
                        sum -= v;
                }
            }
        }
        sumPromise.set_value(sum);
    });

    producer.join();
    consumer.join();
    int sum = sumPromise.get_future().get();

    assert(sum == -4194304);
}

int main()
{
    auto vsLogger = spdlog::create<spdlog::sinks::msvc_sink_mt>("vs_logger");
    spdlog::set_default_logger(vsLogger);

    //datastreamTestSingleThreaded();
    //dataStreamTestMultiThreadPush();
    dataStreamTestProducerConsumer();
}

void testGraphBuilder()
{
    using namespace tasking;

    auto cpuKernel1 = [](gsl::span<const int> input, gsl::span<float> output) {
        for (int i = 0; i < input.size(); i++)
            output[i] = static_cast<float>(input[i]);
    };
    auto cpuKernel2 = [](gsl::span<const float> input, gsl::span<int> output) {
        for (int i = 0; i < input.size(); i++)
            output[i] = static_cast<int>(input[i]);
    };

    TaskPool p {};
    auto t1 = TransformTask<int, float>(p, cpuKernel1);
    auto t2 = TransformTask<float, int>(p, cpuKernel2);

    t1.connect(t2);
}
