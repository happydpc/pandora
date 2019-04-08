#include "stream/source_task.h"
#include "stream/static_data_cache.h"
#include "stream/task_pool.h"
#include "stream/transform_task.h"
#include <hpx/hpx_init.hpp>
#include <hpx/hpx_main.hpp>
#include <hpx/parallel/algorithm.hpp>
#include <random>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/spdlog.h>

void testResourceCacheRandom();
void testResourceCacheLinear();
void testTaskPool();

int main()
{
    auto vsLogger = spdlog::create<spdlog::sinks::msvc_sink_mt>("vs_logger");
    spdlog::set_default_logger(vsLogger);

    testResourceCacheRandom();
    //testResourceCacheLinear();
    //testTaskPool();

    return 0;
}

struct DummyResource {
    std::string path;

    size_t sizeBytes() const { return 1024; }
    static hpx::future<DummyResource> readFromDisk(std::filesystem::path p)
    {
        co_return DummyResource { p.string() };
    }
};

void testResourceCacheRandom()
{
    using namespace tasking;

    using Cache = VariableSizedResourceCache<DummyResource>;
    Cache cache { 1024 * 75, 100 };

    std::vector<Cache::ResourceID> resourceIDs;
    for (int i = 0; i < 100; i++)
        resourceIDs.push_back(cache.addResource(std::to_string(i)));

    std::vector<hpx::future<void>> tasks;
    for (int i = 0; i < 8; i++) {
        tasks.push_back(hpx::async([&cache]() {
            std::random_device dev {};
            std::ranlux24 rng { dev() };
            std::uniform_int_distribution dist { 0, 99 };
            for (int i = 0; i < 5000; i++) {
                Cache::ResourceID resourceID = rng() % 100;

                auto resourceFuture = cache.lookUp(resourceID);
                std::exception_ptr exception = resourceFuture.get_exception_ptr();
                if (exception) {
                    throw exception;
                } else {
                    auto resource = resourceFuture.get();
                    assert(resource->path == std::to_string(resourceID));
                }
            }
        }));
    }

    for (auto& task : tasks)
        task.wait();
}

void testResourceCacheLinear()
{
    using namespace tasking;

    using Cache = VariableSizedResourceCache<DummyResource>;
    Cache cache { 1024 * 20, 10 };

    std::vector<Cache::ResourceID> resourceIDs;
    for (int i = 0; i < 100; i++)
        resourceIDs.push_back(cache.addResource(std::to_string(i)));

    for (const auto resourceID : resourceIDs) {
        auto resource = cache.lookUp(resourceID).get();
    }
}

class RangeSource : public tasking::SourceTask {
public:
    RangeSource(tasking::TaskPool& taskPool, gsl::not_null<tasking::Task<int>*> consumer, int start, int end, int itemsPerBatch)
        : SourceTask(taskPool)
        , m_start(start)
        , m_end(end)
        , m_itemsPerBatch(itemsPerBatch)
        , m_current(start)
        , m_consumer(consumer)
    {
    }

    hpx::future<void> produce() final
    {
        int numItems = static_cast<int>(itemsToProduceUnsafe());

        std::vector<int> data(numItems);
        for (int i = 0; i < numItems; i++) {
            data[i] = m_current++;
            //spdlog::info("Gen: {}", data[i]);
        }
        m_consumer->push(0, std::move(data));
        co_return; // Important! Creates a coroutine.
    }

    size_t itemsToProduceUnsafe() const final
    {
        return std::min(m_end - m_current, m_itemsPerBatch);
    }

private:
    const int m_start, m_end, m_itemsPerBatch;
    int m_current;

    std::atomic_int m_currentlyInPool;

    gsl::not_null<tasking::Task<int>*> m_consumer;
};

int fibonacci(int n)
{
    if (n < 2)
        return n;

    return fibonacci(n - 1) + fibonacci(n - 2);
}

void testTaskPool()
{
    using namespace tasking;

    constexpr int problemSize = 1024;
    constexpr int stepSize = 256;

    std::atomic_int sum = 0;
    auto cpuKernel1 = [&](gsl::span<const int> input, gsl::span<float> output) {
        int localSum = 0;

        for (int i = 0; i < input.size(); i++) {
            output[i] = static_cast<float>(input[i]);

            // Do some dummy work to keep the cores busy.
            int n = fibonacci(4);
            //int n = 0;

            int v = input[i];
            if (v % 2 == 0)
                localSum += v + n;
            else
                localSum -= v + n;
        }

        // Emulate doing 1 microsecond of work for each item.
        //std::this_thread::sleep_for(std::chrono::nanoseconds(stepSize));

        sum += localSum;
    };
    auto cpuKernel2 = [](gsl::span<const float> input, gsl::span<int> output) {
        for (int i = 0; i < input.size(); i++) {
            output[i] = static_cast<int>(input[i]);
        }
    };

    TaskPool p {};
    TransformTask intToFloat(p, cpuKernel1, defaultDataLocalityEstimate);
    //TransformTask<float, int> floatToInt(p, cpuKernel2);
    //intToFloat.connect(&floatToInt);

    RangeSource source = RangeSource(p, &intToFloat, 0, problemSize, stepSize);
    p.run();

    static_assert(problemSize % 2 == 0);
    if (sum == -problemSize / 2)
        spdlog::info("CORRECT ANSWER");
    else
        spdlog::critical("INCORRECT ANSWER");
}
