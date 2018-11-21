#pragma once
#include "metrics/counter.h"
#include "metrics/gauge.h"
#include "metrics/histogram.h"
#include "metrics/offline_exporter.h"
#include "metrics/stats.h"
#include "metrics/stopwatch.h"
#include <chrono>
#include <list>

namespace pandora {

struct RenderStats : public metrics::Stats {
    // Inherit default constructor
    using metrics::Stats::Stats;

    struct {
        std::string sceneFile;
        std::string integrator;
        int spp = 0;
    } config;

    struct {
        metrics::Stopwatch<std::chrono::milliseconds> totalRenderTime;
        metrics::Stopwatch<std::chrono::nanoseconds> svdagTraversalTime;
    } timings;

    struct {
        // Memory loaded/evicted by the scenes TriangleMesh cache
        metrics::Counter<size_t> geometryLoaded { "bytes" };
        metrics::Counter<size_t> geometryEvicted { "bytes" };

        // Memory loaded/evicted by the OOC acceleration structure
        metrics::Counter<size_t> botLevelLoaded { "bytes" };
        metrics::Counter<size_t> botLevelEvicted { "bytes" };

        // Data loaded from disk by the OOC acceleration structure. This might slightly differ from
        // botLevelLoaded because that measures the size of the in-memory representation instead of
        // the serialized (flatbuffer) size.
        metrics::Counter<size_t> oocTotalDiskRead { "bytes" };

        // Memory used by the top-level BVH and the svdags associated with the top-level leaf nodes
        metrics::Counter<size_t> topBVH { "bytes" }; // BVH excluding leafs
        metrics::Counter<size_t> topBVHLeafs { "bytes" }; // leafs (without goemetry)
        metrics::Counter<size_t> batches { "bytes" };

        metrics::Counter<size_t> svdagsBeforeCompression { "bytes" };
        metrics::Counter<size_t> svdagsAfterCompression { "bytes" };
    } memory;

    metrics::Gauge<int> numTopLevelLeafNodes { "nodes" };
    struct FlushInfo {
        int numBatchingPointsWithRays; // Stats about all batching points
        std::vector<size_t> approximateRaysPerFlushedBatchingPoint; // Stats only of flushed batching points
    };
    std::vector<FlushInfo> flushInfos;

    struct {
        metrics::Counter<size_t> numIntersectionTests { "rays" };
        metrics::Counter<size_t> numRaysCulled { "rays" };
    } svdag;

protected:
    nlohmann::json getMetricsSnapshot() const override final;
};

extern metrics::OfflineExporter g_statsOfflineExporter;
extern RenderStats g_stats;

}
