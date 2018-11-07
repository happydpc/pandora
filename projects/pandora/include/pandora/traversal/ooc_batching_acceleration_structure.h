#pragma once
#include "pandora/config.h"
#include "pandora/core/interaction.h"
#include "pandora/core/ray.h"
#include "pandora/core/scene.h"
#include "pandora/core/stats.h"
#include "pandora/eviction/evictable.h"
#include "pandora/flatbuffers/ooc_batching2_generated.h"
#include "pandora/flatbuffers/ooc_batching_generated.h"
#include "pandora/geometry/triangle.h"
#include "pandora/scene/geometric_scene_object.h"
#include "pandora/scene/instanced_scene_object.h"
#include "pandora/svo/sparse_voxel_dag.h"
#include "pandora/svo/voxel_grid.h"
#include "pandora/traversal/bvh/wive_bvh8_build8.h"
#include "pandora/traversal/pauseable_bvh/pauseable_bvh4.h"
#include "pandora/utility/growing_free_list_ts.h"
#include <atomic>
#include <filesystem>
#include <gsl/gsl>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <tbb/concurrent_vector.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/flow_graph.h>
#include <tbb/mutex.h>
#include <tbb/parallel_for_each.h>
#include <tbb/parallel_sort.h>
#include <tbb/scalable_allocator.h>
#include <tbb/task_group.h>
#include <thread>

// For file I/O
#ifdef __linux__
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace std::string_literals;

namespace pandora {

#ifdef D_OUT_OF_CORE_CACHE_FOLDER
// Dumb macro magic so I can move the cache folder to my slower SSD on my local machine, remove from the final build!!!
// https://stackoverflow.com/questions/6852920/how-do-i-turn-a-macro-into-a-string-using-cpp
#define QUOTE(x) #x
#define STR(x) QUOTE(x)
static const std::filesystem::path OUT_OF_CORE_CACHE_FOLDER(STR(D_OUT_OF_CORE_CACHE_FOLDER));
#else
static const std::filesystem::path OUT_OF_CORE_CACHE_FOLDER("ooc_node_cache/");
#endif

class DiskDataBatcher {
public:
    DiskDataBatcher(std::filesystem::path outFolder, std::string_view filePrefix, size_t maxFileSize)
        : m_outFolder(outFolder)
        , m_filePrefix(filePrefix)
        , m_maxFileSize(maxFileSize)
        , m_currentFileID(0)
        , m_currentFileSize(0)
    {
        openNewFile();
    }

#ifdef __linux__
    struct RAIIBuffer {
    public:
        inline RAIIBuffer(std::filesystem::path filePath, size_t offset, size_t size)
        {
            // Linux does not support O_DIRECT in combination with memory mapped I/O (meaning we cannot bypass the
            // OS file cache). So instead we use posix I/O on Linux giving us the option to bypass the file cache at
            // the cost of an extra allocation & copy.
            int flags = O_RDONLY;
            if constexpr (OUT_OF_CORE_DISABLE_FILE_CACHING) {
                flags |= O_DIRECT;
            }
            auto filedesc = open(filePath.string().c_str(), flags);
            ALWAYS_ASSERT(filedesc >= 0);

            constexpr int alignment = 512; // Block size
            //size_t fileSize = std::filesystem::file_size(cacheFilePath);
            size_t r = size % alignment;
            size_t bufferSize = r ? size - r + alignment : size;

            m_buffer((char*)aligned_alloc(alignment, bufferSize), deleter);
            fseek(fileDesc, offset, SEEK_SET);
            ALWAYS_ASSERT(read(filedesc, m_buffer.get(), bufferSize) >= 0);
            close(filedesc);
        }

        inline const void* data() const
        {
            return m_data.data();
        }

    private:
        static auto deleter = [](char* ptr) {
            free(ptr);
        };
        std::unique_ptr<char[], decltype(deleter)> m_buffer;
    };

#else
    struct RAIIBuffer {
    public:
        inline RAIIBuffer(std::filesystem::path filePath, size_t offset, size_t size)
        {
            int fileFlags = OUT_OF_CORE_DISABLE_FILE_CACHING ? mio::access_flags::no_buffering : 0;
            m_data = mio::mmap_source(filePath.string(), offset, size, fileFlags);
        }

        inline const void* data() const
        {
            return m_data.data();
        }

    private:
        mio::mmap_source m_data;
    };
#endif

    struct FilePart {
        // Shared between different file fragments to save memory
        std::shared_ptr<std::filesystem::path> filePathSharedPtr;
        size_t offset; // bytes
        size_t size; // bytes

        RAIIBuffer load() const
        {
            return RAIIBuffer(*filePathSharedPtr, offset, size);
        }
    };

    inline FilePart writeData(const flatbuffers::FlatBufferBuilder& fbb)
    {
        std::scoped_lock<std::mutex> l(m_accessMutex);

        FilePart ret = {
            m_currentFilePathSharedPtr,
            m_currentFileSize,
            fbb.GetSize()
        };
        m_file.write(reinterpret_cast<const char*>(fbb.GetBufferPointer()), fbb.GetSize());
        m_currentFileSize += fbb.GetSize();

        if (m_currentFileSize > m_maxFileSize) {
            openNewFile();
        }
        assert(m_currentFileSize == m_file.tellp());
        return ret;
    }

    inline void flush()
    {
        m_file.flush();
    }

private:
    inline std::filesystem::path fullPath(int fileID)
    {
        return m_outFolder / (m_filePrefix + std::to_string(fileID) + ".bin"s);
    }

    inline void openNewFile()
    {
        m_currentFilePathSharedPtr = std::make_shared<std::filesystem::path>(fullPath(++m_currentFileID));
        m_file = std::ofstream(*m_currentFilePathSharedPtr, std::ios::binary | std::ios::trunc);
        m_currentFileSize = 0;
    }

private:
    const std::filesystem::path m_outFolder;
    const std::string m_filePrefix;
    const size_t m_maxFileSize;

    std::mutex m_accessMutex;

    int m_currentFileID;
    std::shared_ptr<std::filesystem::path> m_currentFilePathSharedPtr;
    std::ofstream m_file;
    size_t m_currentFileSize;
};

template <typename UserState, size_t BatchSize = 32>
class OOCBatchingAccelerationStructure {
public:
    using InsertHandle = void*;
    using HitCallback = std::function<void(const Ray&, const SurfaceInteraction&, const UserState&)>;
    using AnyHitCallback = std::function<void(const Ray&, const UserState&)>;
    using MissCallback = std::function<void(const Ray&, const UserState&)>;

public:
    OOCBatchingAccelerationStructure(
        const Scene& scene,
        HitCallback hitCallback, AnyHitCallback anyHitCallback, MissCallback missCallback);
    ~OOCBatchingAccelerationStructure() = default;

    void placeIntersectRequests(gsl::span<const Ray> rays, gsl::span<const UserState> perRayUserData, const InsertHandle& insertHandle = nullptr);
    void placeIntersectAnyRequests(gsl::span<const Ray> rays, gsl::span<const UserState> perRayUserData, const InsertHandle& insertHandle = nullptr);

    void flush();

private:
    struct RayBatch {
    public:
        RayBatch(RayBatch* nextPtr = nullptr)
            : m_nextPtr(nextPtr)
        {
        }
        void setNext(RayBatch* nextPtr) { m_nextPtr = nextPtr; }
        RayBatch* next() { return m_nextPtr; }
        const RayBatch* next() const { return m_nextPtr; }
        bool full() const { return m_data.full(); }

        bool tryPush(const Ray& ray, const RayHit& rayHit, const UserState& state, const PauseableBVHInsertHandle& insertHandle);
        bool tryPush(const Ray& ray, const UserState& state, const PauseableBVHInsertHandle& insertHandle);

        size_t size() const
        {
            return m_data.size();
        }

        size_t sizeBytes() const
        {
            return sizeof(decltype(*this));
        }

        // https://www.fluentcpp.com/2018/05/08/std-iterator-deprecated/
        struct iterator {
        public:
            using iterator_category = std::random_access_iterator_tag;
            using value_type = std::tuple<Ray&, std::optional<RayHit>&, UserState&, PauseableBVHInsertHandle&>;
            using difference_type = std::ptrdiff_t;
            using pointer = value_type*;
            using reference = value_type&;

            explicit iterator(RayBatch* batch, size_t index);
            iterator& operator++(); // pre-increment
            iterator operator++(int); // post-increment
            bool operator==(iterator other) const;
            bool operator!=(iterator other) const;
            value_type operator*();

        private:
            RayBatch* m_rayBatch;
            size_t m_index;
        };

        const iterator begin();
        const iterator end();

    private:
        // NOTE: using a std::array here is expensive because the default constructor of all items will be called when the batch is created
        // The larger the batches, the more expensive this becomes
        struct BatchItem {
            BatchItem(const Ray& ray, const UserState& userState, const PauseableBVHInsertHandle& insertHandle)
                : ray(ray)
                , hitInfo({})
                , userState(userState)
                , insertHandle(insertHandle)
            {
            }
            BatchItem(const Ray& ray, const RayHit& hitInfo, const UserState& userState, const PauseableBVHInsertHandle& insertHandle)
                : ray(ray)
                , hitInfo(hitInfo)
                , userState(userState)
                , insertHandle(insertHandle)
            {
            }
            Ray ray;
            std::optional<RayHit> hitInfo;
            UserState userState;
            PauseableBVHInsertHandle insertHandle;
        };
        eastl::fixed_vector<BatchItem, BatchSize> m_data;

        RayBatch* m_nextPtr;
    };

    class BotLevelLeafNodeInstanced {
    public:
        BotLevelLeafNodeInstanced(const SceneObjectGeometry* baseSceneObjectGeometry, unsigned primitiveID);

        Bounds getBounds() const;
        bool intersect(Ray& ray, RayHit& hitInfo) const;

    private:
        const SceneObjectGeometry* m_baseSceneObjectGeometry;
        const unsigned m_primitiveID;
    };

    class BotLevelLeafNode {
    public:
        BotLevelLeafNode(const OOCGeometricSceneObject* sceneObject, const std::shared_ptr<SceneObjectGeometry>& sceneObjectGeometry, unsigned primitiveID);
        BotLevelLeafNode(const OOCInstancedSceneObject* sceneObject, const std::shared_ptr<SceneObjectGeometry>& sceneObjectGeometry, const std::shared_ptr<WiVeBVH8Build8<BotLevelLeafNodeInstanced>>& bvh);

        Bounds getBounds() const;
        bool intersect(Ray& ray, RayHit& hitInfo) const;

    private:
        struct Regular {

            const OOCGeometricSceneObject* sceneObject;
            const std::shared_ptr<SceneObjectGeometry> sceneObjectGeometry;
            const unsigned primitiveID;
        };

        struct Instance {
            const OOCInstancedSceneObject* sceneObject;
            const std::shared_ptr<SceneObjectGeometry> sceneObjectGeometry;
            std::shared_ptr<WiVeBVH8Build8<BotLevelLeafNodeInstanced>> bvh;
        };
        std::variant<Regular, Instance> m_data;
    };

    //using CachedInstanceData = std::pair<std::shared_ptr<GeometricSceneObjectGeometry>, std::shared_ptr<WiVeBVH8Build8<BotLevelLeafNodeInstanced>>>;
    struct CachedInstanceData {
        size_t sizeBytes() const
        {
            return geometry->sizeBytes() + bvh->sizeBytes();
        }

        std::shared_ptr<GeometricSceneObjectGeometry> geometry;
        std::shared_ptr<WiVeBVH8Build8<BotLevelLeafNodeInstanced>> bvh;
    };
    struct CachedBatchingPoint {
        size_t sizeBytes() const
        {
            size_t size = sizeof(decltype(*this));
            size += leafBVH.sizeBytes();
            size += geometrySize;
            size += geometryOwningPointers.size() * sizeof(decltype(geometryOwningPointers)::value_type);
            return size;
        }

        size_t geometrySize = 0; // Simply iterating over geometryOwningPointers is incorrect because we would count instanced geometry multiple times
        WiVeBVH8Build8<BotLevelLeafNode> leafBVH;
        std::vector<std::shared_ptr<SceneObjectGeometry>> geometryOwningPointers;
    };
    using MyCacheT = CacheT<CachedInstanceData, CachedBatchingPoint>;

    class TopLevelLeafNode {
    public:
        TopLevelLeafNode(
            std::filesystem::path cacheFile,
            gsl::span<const OOCSceneObject*> sceneObjects,
            const std::unordered_map<const OOCGeometricSceneObject*, EvictableResourceHandle<CachedInstanceData, MyCacheT>>& serializedInstanceBaseSceneObject,
            MyCacheT* geometryCache,
            OOCBatchingAccelerationStructure<UserState, BatchSize>* accelerationStructurePtr);
        TopLevelLeafNode(TopLevelLeafNode&& other);

        Bounds getBounds() const;

        std::optional<bool> intersect(Ray& ray, RayHit& hitInfo, const UserState& userState, PauseableBVHInsertHandle insertHandle) const; // Batches rays. This function is thread safe.
        std::optional<bool> intersectAny(Ray& ray, const UserState& userState, PauseableBVHInsertHandle insertHandle) const; // Batches rays. This function is thread safe.

        void forceLoad() { m_accelerationStructurePtr->m_geometryCache.getBlocking<CachedBatchingPoint>(m_geometryDataCacheID); }
        bool inCache() const { return m_accelerationStructurePtr->m_geometryCache.inCache(m_geometryDataCacheID); }
        bool hasFullBatches() { return m_immutableRayBatchList.load() != nullptr; }
        bool forwardPartiallyFilledBatches(); // Adds the active batches to the list of immutable batches (even if they are not full)
        // Flush a whole range of nodes at a time as opposed to a non-static flush member function which would require a
        // separate tbb flow graph for each node that is processed.
        static void flushRange(
            gsl::span<TopLevelLeafNode*> nodes,
            OOCBatchingAccelerationStructure<UserState, BatchSize>* accelerationStructurePtr);

        static void compressSVDAGs(gsl::span<TopLevelLeafNode*> nodes);

        size_t sizeBytes() const;
        size_t diskSizeBytes() const;

    private:
        static EvictableResourceID generateCachedBVH(
            std::filesystem::path cacheFile,
            gsl::span<const OOCSceneObject*> sceneObjects,
            const std::unordered_map<const OOCGeometricSceneObject*, EvictableResourceHandle<CachedInstanceData, MyCacheT>>& serializedInstanceBaseSceneObject,
            MyCacheT* cache);

        struct SVDAGRayOffset {
            glm::vec3 gridBoundsMin;
            glm::vec3 invGridBoundsExtent;
        };
        static std::pair<SparseVoxelDAG, SVDAGRayOffset> computeSVDAG(gsl::span<const OOCSceneObject*> sceneObjects);

    private:
        EvictableResourceID m_geometryDataCacheID;
        std::vector<const OOCSceneObject*> m_sceneObjects;
        const size_t m_diskSize;

        std::atomic_int m_numFullBatches;
        tbb::enumerable_thread_specific<RayBatch*> m_threadLocalActiveBatch;
        std::atomic<RayBatch*> m_immutableRayBatchList;
        OOCBatchingAccelerationStructure<UserState, BatchSize>* m_accelerationStructurePtr;

        std::pair<SparseVoxelDAG, SVDAGRayOffset> m_svdagAndTransform;
    };

    static PauseableBVH4<TopLevelLeafNode, UserState> buildBVH(
        MyCacheT* cache,
        const Scene& scene,
        OOCBatchingAccelerationStructure<UserState, BatchSize>* accelerationStructurePtr);

private:
    const int m_numLoadingThreads;
    MyCacheT m_geometryCache;

    GrowingFreeListTS<RayBatch> m_batchAllocator;
    //tbb::scalable_allocator<RayBatch> m_batchAllocator;

    PauseableBVH4<TopLevelLeafNode, UserState> m_bvh;

    HitCallback m_hitCallback;
    AnyHitCallback m_anyHitCallback;
    MissCallback m_missCallback;
};

template <typename UserState, size_t BatchSize>
inline OOCBatchingAccelerationStructure<UserState, BatchSize>::OOCBatchingAccelerationStructure(
    const Scene& scene,
    HitCallback hitCallback, AnyHitCallback anyHitCallback, MissCallback missCallback)
    : m_numLoadingThreads(2 * std::thread::hardware_concurrency())
    , m_geometryCache(
          OUT_OF_CORE_MEMORY_LIMIT,
          m_numLoadingThreads,
          [](size_t bytes) { g_stats.memory.botLevelLoaded += bytes; },
          [](size_t bytes) { g_stats.memory.botLevelEvicted += bytes; })
    , m_batchAllocator()
    , m_bvh(std::move(buildBVH(&m_geometryCache, scene, this)))
    , m_hitCallback(hitCallback)
    , m_anyHitCallback(anyHitCallback)
    , m_missCallback(missCallback)
{
    // Clean the scenes geometry cache because it won't be used anymore. The batches recreate the geometry
    // and use their own cache to manage it.
    scene.geometryCache()->evictAllUnsafe();

    g_stats.memory.topBVH += m_bvh.sizeBytes();
    for (const auto* leaf : m_bvh.leafs()) {
        g_stats.memory.topBVHLeafs += leaf->sizeBytes();
    }
}

template <typename UserState, size_t BatchSize>
inline void OOCBatchingAccelerationStructure<UserState, BatchSize>::placeIntersectRequests(
    gsl::span<const Ray> rays,
    gsl::span<const UserState> perRayUserData,
    const InsertHandle& insertHandle)
{
    (void)insertHandle;
    assert(perRayUserData.size() == rays.size());

    for (int i = 0; i < rays.size(); i++) {
        RayHit rayHit;
        Ray ray = rays[i]; // Copy so we can mutate it
        UserState userState = perRayUserData[i];

        auto optResult = m_bvh.intersect(ray, rayHit, userState);
        if (optResult && *optResult == false) {
            // If we get a result directly it must be because we missed the scene
            m_missCallback(ray, userState);
        }
    }
}

template <typename UserState, size_t BatchSize>
inline void OOCBatchingAccelerationStructure<UserState, BatchSize>::placeIntersectAnyRequests(
    gsl::span<const Ray> rays,
    gsl::span<const UserState> perRayUserData,
    const InsertHandle& insertHandle)
{
    (void)insertHandle;
    assert(perRayUserData.size() == rays.size());

    for (int i = 0; i < rays.size(); i++) {
        Ray ray = rays[i]; // Copy so we can mutate it
        UserState userState = perRayUserData[i];

        auto optResult = m_bvh.intersectAny(ray, userState);
        if (optResult && *optResult == false) {
            // If we get a result directly it must be because we missed the scene
            m_missCallback(ray, userState);
        }
    }
}

template <typename UserState, size_t BatchSize>
inline PauseableBVH4<typename OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode, UserState> OOCBatchingAccelerationStructure<UserState, BatchSize>::buildBVH(
    MyCacheT* cache,
    const Scene& scene,
    OOCBatchingAccelerationStructure<UserState, BatchSize>* accelerationStructurePtr)
{
    if (!std::filesystem::exists(OUT_OF_CORE_CACHE_FOLDER)) {
        std::cout << "Create cache folder: " << OUT_OF_CORE_CACHE_FOLDER << std::endl;
        std::filesystem::create_directories(OUT_OF_CORE_CACHE_FOLDER);
    }
    ALWAYS_ASSERT(std::filesystem::is_directory(OUT_OF_CORE_CACHE_FOLDER));

    std::cout << "Building BVHs for all instanced geometry and storing them to disk\n";

    // Collect scene objects that are referenced by instanced scene objects.
    std::unordered_set<const OOCGeometricSceneObject*> instancedBaseSceneObjects;
    for (const auto* sceneObject : scene.getOOCSceneObjects()) {
        if (const auto* instancedSceneObject = dynamic_cast<const OOCInstancedSceneObject*>(sceneObject)) {
            instancedBaseSceneObjects.insert(instancedSceneObject->getBaseObject());
        }
    }

    // For each of those objects build a BVH and store the object+BVH on disk
    DiskDataBatcher fileBatcher(OUT_OF_CORE_CACHE_FOLDER, "instanced_object", 500 * 1024 * 1024); // Batch instanced geometry into files of 500MB
    std::unordered_map<const OOCGeometricSceneObject*, EvictableResourceHandle<CachedInstanceData, MyCacheT>> instancedBVHs;
    int instanceBaseFileNum = 0;
    for (const auto* instancedBaseSceneObject : instancedBaseSceneObjects) { // TODO: parallelize
        auto geometry = instancedBaseSceneObject->getGeometryBlocking();

        std::vector<BotLevelLeafNodeInstanced> leafs;
        leafs.reserve(geometry->numPrimitives());
        for (unsigned primitiveID = 0; primitiveID < geometry->numPrimitives(); primitiveID++) {
            leafs.push_back(BotLevelLeafNodeInstanced(geometry.get(), primitiveID));
        }

        // NOTE: the "geometry" variable ensures that the geometry pointed to stays in memory for the BVH build
        //       (which requires the geometry to determine the leaf node bounds).
        WiVeBVH8Build8<BotLevelLeafNodeInstanced> bvh(leafs);

        // Serialize
        flatbuffers::FlatBufferBuilder fbb;
        const auto* rawGeometry = dynamic_cast<const GeometricSceneObjectGeometry*>(geometry.get());
        ALWAYS_ASSERT(rawGeometry != nullptr);
        auto serializedGeometry = rawGeometry->serialize(fbb);
        auto serializedBVH = bvh.serialize(fbb);
        auto serializedBaseSceneObject = serialization::CreateOOCBatchingBaseSceneObject(
            fbb,
            serializedGeometry,
            serializedBVH);
        fbb.Finish(serializedBaseSceneObject);

        // Write to disk
        auto filePart = fileBatcher.writeData(fbb);

        // Callback to restore the data we have just written to disk
        auto resourceID = cache->emplaceFactoryThreadSafe<CachedInstanceData>([filePart]() -> CachedInstanceData {
            auto buffer = filePart.load();
            auto serializedBaseSceneObject = serialization::GetOOCBatchingBaseSceneObject(buffer.data());
            auto geometry = std::make_shared<GeometricSceneObjectGeometry>(serializedBaseSceneObject->base_geometry());

            if constexpr (ENABLE_ADDITIONAL_STATISTICS) {
                g_stats.memory.oocTotalDiskRead += filePart.size;
            }

            std::vector<BotLevelLeafNodeInstanced> leafs;
            leafs.reserve(geometry->numPrimitives());
            for (unsigned primitiveID = 0; primitiveID < geometry->numPrimitives(); primitiveID++) {
                leafs.push_back(BotLevelLeafNodeInstanced(geometry.get(), primitiveID));
            }

            auto bvh = std::make_shared<WiVeBVH8Build8<BotLevelLeafNodeInstanced>>(serializedBaseSceneObject->bvh(), std::move(leafs));
            return CachedInstanceData { geometry, bvh };
        });
        instancedBVHs.insert({ instancedBaseSceneObject, EvictableResourceHandle<CachedInstanceData, MyCacheT>(cache, resourceID) });
    }
    fileBatcher.flush();

    std::cout << "Creating scene object groups" << std::endl;
    auto sceneObjectGroups = scene.groupOOCSceneObjects(OUT_OF_CORE_BATCHING_PRIMS_PER_LEAF);

    std::cout << "Creating leaf nodes" << std::endl;
    std::mutex m;
    std::vector<TopLevelLeafNode> leafs;
    tbb::parallel_for(tbb::blocked_range<size_t>(0llu, sceneObjectGroups.size()), [&](tbb::blocked_range<size_t> localRange) {
        for (size_t i = localRange.begin(); i < localRange.end(); i++) {
            std::filesystem::path cacheFile = OUT_OF_CORE_CACHE_FOLDER / ("node"s + std::to_string(i) + ".bin"s);
            TopLevelLeafNode leaf(cacheFile, sceneObjectGroups[i], instancedBVHs, cache, accelerationStructurePtr);
            {
                std::scoped_lock<std::mutex> l(m);
                leafs.push_back(std::move(leaf));
            }
        }
    });

    /*std::cout << "Force loading BVH leaf nodes for debugging purposes" << std::endl;
    for (auto& leaf : leafs) {
        leaf.forceLoad();
    }
    std::cout << "Bot level structures bytes used: " << g_stats.memory.botLevelLoaded << std::endl;
    system("PAUSE");
    exit(1);*/

    g_stats.numTopLevelLeafNodes += leafs.size();

    std::cout << "Building top-level BVH" << std::endl;
    auto ret = PauseableBVH4<TopLevelLeafNode, UserState>(leafs);
    TopLevelLeafNode::compressSVDAGs(ret.leafs());
    return std::move(ret);
}

template <typename UserState, size_t BatchSize>
inline OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::TopLevelLeafNode(
    std::filesystem::path cacheFile,
    gsl::span<const OOCSceneObject*> sceneObjects,
    const std::unordered_map<const OOCGeometricSceneObject*, EvictableResourceHandle<CachedInstanceData, MyCacheT>>& baseSceneObjectCacheHandles,
    MyCacheT* geometryCache,
    OOCBatchingAccelerationStructure<UserState, BatchSize>* accelerationStructure)
    : m_geometryDataCacheID(generateCachedBVH(cacheFile, sceneObjects, baseSceneObjectCacheHandles, geometryCache))
    , m_diskSize(std::filesystem::file_size(cacheFile))
    , m_numFullBatches(0)
    , m_threadLocalActiveBatch([]() { return nullptr; })
    , m_immutableRayBatchList(nullptr)
    , m_accelerationStructurePtr(accelerationStructure)
    , m_svdagAndTransform(computeSVDAG(sceneObjects))
{
    m_sceneObjects.insert(std::end(m_sceneObjects), std::begin(sceneObjects), std::end(sceneObjects));
}

template <typename UserState, size_t BatchSize>
inline OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::TopLevelLeafNode(TopLevelLeafNode&& other)
    : m_geometryDataCacheID(other.m_geometryDataCacheID)
    , m_sceneObjects(std::move(other.m_sceneObjects))
    , m_diskSize(other.m_diskSize)
    , m_numFullBatches(other.m_numFullBatches.load())
    , m_threadLocalActiveBatch(std::move(other.m_threadLocalActiveBatch))
    , m_immutableRayBatchList(other.m_immutableRayBatchList.load())
    , m_accelerationStructurePtr(other.m_accelerationStructurePtr)
    , m_svdagAndTransform(std::move(other.m_svdagAndTransform))
{
}

template <typename UserState, size_t BatchSize>
inline EvictableResourceID OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::generateCachedBVH(
    std::filesystem::path cacheFilePath,
    gsl::span<const OOCSceneObject*> sceneObjects,
    const std::unordered_map<const OOCGeometricSceneObject*, EvictableResourceHandle<CachedInstanceData, MyCacheT>>& instanceBaseSceneObjectHandleMapping,
    MyCacheT* cache)
{
    flatbuffers::FlatBufferBuilder fbb;
    std::vector<flatbuffers::Offset<serialization::GeometricSceneObjectGeometry>> serializedUniqueGeometry;
    std::vector<flatbuffers::Offset<serialization::InstancedSceneObjectGeometry>> serializedInstancedGeometry;

    std::vector<std::shared_ptr<SceneObjectGeometry>> geometryOwningPointers; // Keep geometry alive until BVH build finished

    // Create leaf nodes for all instanced geometry and then for all non-instanced geometry. It is important to keep these
    // two separate so we can recreate the leafs in the exact same order when deserializing the BVH.
    std::vector<BotLevelLeafNode> leafs;
    std::vector<const OOCGeometricSceneObject*> geometricSceneObjects;
    std::vector<const OOCInstancedSceneObject*> instancedSceneObjects;
    std::vector<EvictableResourceHandle<CachedInstanceData, MyCacheT>> instanceBaseResourceHandles; // ResourceID into the cache to load an instanced base scene object
    for (const auto* sceneObject : sceneObjects) {
        if (const auto* geometricSceneObject = dynamic_cast<const OOCGeometricSceneObject*>(sceneObject)) {
            // Serialize
            std::shared_ptr<SceneObjectGeometry> geometryOwningPointer = geometricSceneObject->getGeometryBlocking();
            const auto* geometry = dynamic_cast<const GeometricSceneObjectGeometry*>(geometryOwningPointer.get());
            serializedUniqueGeometry.push_back(
                geometry->serialize(fbb));
            geometricSceneObjects.push_back(geometricSceneObject);

            // Create bot-level leaf node
            for (unsigned primitiveID = 0; primitiveID < geometry->numPrimitives(); primitiveID++) {
                leafs.push_back(BotLevelLeafNode(geometricSceneObject, geometryOwningPointer, primitiveID));
            }

            geometryOwningPointers.emplace_back(geometryOwningPointer); // Keep geometry alive until BVH build finished
        }
    }
    for (const auto* sceneObject : sceneObjects) {
        if (const auto* instancedSceneObject = dynamic_cast<const OOCInstancedSceneObject*>(sceneObject)) {
            const auto* baseObject = instancedSceneObject->getBaseObject();
            assert(instanceBaseSceneObjectHandleMapping.find(baseObject) != instanceBaseSceneObjectHandleMapping.end());

            // Remember how to construct the base object geometry
            instanceBaseResourceHandles.push_back(instanceBaseSceneObjectHandleMapping.find(baseObject)->second);

            // Store the instancing geometry (which does not store the base object geometry but relies on us to do so)
            auto dummyGeometry = instancedSceneObject->getDummyGeometryBlocking();
            serializedInstancedGeometry.push_back(dummyGeometry.serialize(fbb));

            // Create bot-level node
            leafs.push_back(BotLevelLeafNode(instancedSceneObject, nullptr, nullptr)); // No need to load the underlying geometry & BVH
            instancedSceneObjects.push_back(instancedSceneObject);
        }
    }

    WiVeBVH8Build8<BotLevelLeafNode> bvh(leafs);
    auto serializedBVH = bvh.serialize(fbb);

    auto serializedTopLevelLeafNode = serialization::CreateOOCBatchingTopLevelLeafNode(
        fbb,
        fbb.CreateVector(serializedUniqueGeometry),
        fbb.CreateVector(serializedInstancedGeometry),
        serializedBVH,
        static_cast<uint32_t>(leafs.size()));
    fbb.Finish(serializedTopLevelLeafNode);

    // Re-use existing cache files and prevent unnecessary writes (reduces SSD lifespan)
    if (!std::filesystem::exists(cacheFilePath)) {
        std::ofstream file;
        file.open(cacheFilePath, std::ios::out | std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(fbb.GetBufferPointer()), fbb.GetSize());
        file.close();
    }

    auto resourceID = cache->emplaceFactoryThreadSafe<CachedBatchingPoint>([cacheFilePath, geometricSceneObjects = std::move(geometricSceneObjects), instancedSceneObjects = std::move(instancedSceneObjects),
                                                                               instanceBaseResourceHandles = std::move(instanceBaseResourceHandles)]() -> CachedBatchingPoint {
#ifdef __linux__
        // Linux does not support O_DIRECT in combination with memory mapped I/O (meaning we cannot bypass the
        // OS file cache). So instead we use posix I/O on Linux giving us the option to bypass the file cache at
        // the cost of an extra allocation & copy.
        int flags = O_RDONLY;
        if constexpr (OUT_OF_CORE_DISABLE_FILE_CACHING) {
            flags |= O_DIRECT;
        }
        auto filedesc = open(cacheFilePath.string().c_str(), flags);
        ALWAYS_ASSERT(filedesc >= 0);

        constexpr int alignment = 512; // Block size
        size_t fileSize = std::filesystem::file_size(cacheFilePath);
        size_t r = fileSize % alignment;
        size_t bufferSize = r ? fileSize - r + alignment : fileSize;

        auto deleter = [](char* ptr) {
            free(ptr);
        };
        std::unique_ptr<char[], decltype(deleter)> buffer((char*)aligned_alloc(alignment, bufferSize), deleter);
        ALWAYS_ASSERT(read(filedesc, buffer.get(), bufferSize) >= 0);
        close(filedesc);

        auto serializedTopLevelLeafNode = serialization::GetOOCBatchingTopLevelLeafNode(buffer.get());

        /*std::ifstream file(cacheFilePath, std::ios::binary | std::ios::ate);
        assert(file.is_open());
        auto pos = file.tellg();
        
        auto chars = std::make_unique<char[]>(pos);
        file.seekg(0, std::ios::beg);
        file.read(chars.get(), pos);
        file.close();

        auto serializedTopLevelLeafNode = serialization::GetOOCBatchingTopLevelLeafNode(chars.get());*/
#else
        int fileFlags = OUT_OF_CORE_DISABLE_FILE_CACHING ? mio::access_flags::no_buffering : 0;
        auto mmapFile = mio::mmap_source(cacheFilePath.string(), 0, mio::map_entire_file, fileFlags);
        auto serializedTopLevelLeafNode = serialization::GetOOCBatchingTopLevelLeafNode(mmapFile.data());
#endif

        if constexpr (ENABLE_ADDITIONAL_STATISTICS) {
            g_stats.memory.oocTotalDiskRead += std::filesystem::file_size(cacheFilePath);
        }

        //const auto* serializedInstanceBaseBVHs = serializedTopLevelLeafNode->instance_base_bvh();
        //const auto* serializedInstanceBaseGeometry = serializedTopLevelLeafNode->instance_base_geometry();

        size_t geometrySize = 0;
        std::vector<std::shared_ptr<SceneObjectGeometry>> geometryOwningPointers;

        // Load geometry/BVH of geometric nodes that are referenced by instancing nodes
        assert(instancedSceneObjects.size() == instanceBaseResourceHandles.size());
        std::vector<std::shared_ptr<CachedInstanceData>> instanceBaseObjects;
        for (const auto resourceHandle : instanceBaseResourceHandles) {
            std::shared_ptr<CachedInstanceData> cachedInstanceBaseData = resourceHandle.getBlocking();
            instanceBaseObjects.push_back(cachedInstanceBaseData);
        }

        // Load unique geometric scene objects
        const auto* serializedUniqueGeometry = serializedTopLevelLeafNode->unique_geometry();
        const auto* serializedInstancedGeometry = serializedTopLevelLeafNode->instanced_geometry();
        std::vector<BotLevelLeafNode> leafs;
        leafs.reserve(serializedTopLevelLeafNode->num_bot_level_leafs());
        for (unsigned i = 0; i < geometricSceneObjects.size(); i++) {
            auto geometry = std::make_shared<GeometricSceneObjectGeometry>(serializedUniqueGeometry->Get(i));

            for (unsigned primitiveID = 0; primitiveID < geometry->numPrimitives(); primitiveID++) {
                leafs.emplace_back(geometricSceneObjects[i], geometry, primitiveID);
            }

            geometrySize += geometry->sizeBytes();
            geometryOwningPointers.emplace_back(std::move(geometry));
        }

        // Load instanced scene objects
        //const auto* serializedInstancedIDs = serializedTopLevelLeafNode->instanced_ids();
        assert(instanceBaseObjects.size() == serializedInstancedGeometry->Length());
        for (size_t i = 0; i < instanceBaseObjects.size(); i++) {
            const auto& [baseGeometry, baseBVH] = *instanceBaseObjects[i];

            auto geometry = std::make_shared<InstancedSceneObjectGeometry>(
                serializedInstancedGeometry->Get(static_cast<flatbuffers::uoffset_t>(i)),
                baseGeometry);
            leafs.push_back(BotLevelLeafNode(instancedSceneObjects[i], geometry, baseBVH));

            geometryOwningPointers.emplace_back(std::move(geometry));
        }

        auto bvh = WiVeBVH8Build8<BotLevelLeafNode>(serializedTopLevelLeafNode->bvh(), std::move(leafs));
        return CachedBatchingPoint {
            geometrySize,
            std::move(bvh),
            std::move(geometryOwningPointers)
        };
    });
    return resourceID;
}

template <typename UserState, size_t BatchSize>
inline std::pair<SparseVoxelDAG, typename OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::SVDAGRayOffset> OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::computeSVDAG(gsl::span<const OOCSceneObject*> sceneObjects)
{
    Bounds gridBounds;
    for (const auto* sceneObject : sceneObjects) {
        gridBounds.extend(sceneObject->worldBounds());
    }

    VoxelGrid voxelGrid(OUT_OF_CORE_SVDAG_RESOLUTION);
    for (const auto* sceneObject : sceneObjects) {
        auto geometry = sceneObject->getGeometryBlocking();
        geometry->voxelize(voxelGrid, gridBounds);
    }

    // SVO is at (1, 1, 1) to (2, 2, 2)
    float maxDim = maxComponent(gridBounds.extent());

    SparseVoxelDAG svdag(voxelGrid);
    // NOTE: the svdags are already being compressed together. Compressing here too will cost more compute power but will reduce memory usage.
    /*{
        std::vector svdags = { &svdag };
        compressDAGs(svdags);
    }*/
    return { std::move(svdag), SVDAGRayOffset { gridBounds.min, glm::vec3(1.0f / maxDim) } };
}

template <typename UserState, size_t BatchSize>
inline Bounds OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::getBounds() const
{
    Bounds ret;
    for (const auto* sceneObject : m_sceneObjects) {
        ret.extend(sceneObject->worldBounds());
    }
    return ret;
}

template <typename UserState, size_t BatchSize>
inline std::optional<bool> OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::intersect(
    Ray& ray,
    RayHit& hitInfo,
    const UserState& userState,
    PauseableBVHInsertHandle insertHandle) const
{
    if constexpr (OUT_OF_CORE_OCCLUSION_CULLING) {
        //auto scopedTimings = g_stats.timings.svdagTraversalTime.getScopedStopwatch();

        const auto& [svdag, svdagRayOffset] = m_svdagAndTransform;
        auto svdagRay = ray;
        svdagRay.origin = glm::vec3(1.0f) + (svdagRayOffset.invGridBoundsExtent * (ray.origin - svdagRayOffset.gridBoundsMin));
        auto tOpt = svdag.intersectScalar(svdagRay);
        if (!tOpt)
            return false; // Missed, continue traversal
    }

    auto* mutThisPtr = const_cast<TopLevelLeafNode*>(this);

    RayBatch* batch = mutThisPtr->m_threadLocalActiveBatch.local();
    if (!batch || batch->full()) {
        if (batch) {
            // Batch was full, move it to the list of immutable batches
            auto* oldHead = mutThisPtr->m_immutableRayBatchList.load();
            do {
                batch->setNext(oldHead);
            } while (!mutThisPtr->m_immutableRayBatchList.compare_exchange_weak(oldHead, batch));

            mutThisPtr->m_numFullBatches.fetch_add(1);
        }

        // Allocate a new batch and set it as the new active batch
        auto* mem = mutThisPtr->m_accelerationStructurePtr->m_batchAllocator.allocate();
        batch = new (mem) RayBatch();
        mutThisPtr->m_threadLocalActiveBatch.local() = batch;
    }

    bool success = batch->tryPush(ray, hitInfo, userState, insertHandle);
    assert(success);

    return {}; // Paused
}

template <typename UserState, size_t BatchSize>
inline std::optional<bool> OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::intersectAny(Ray& ray, const UserState& userState, PauseableBVHInsertHandle insertHandle) const
{
    if constexpr (OUT_OF_CORE_OCCLUSION_CULLING) {
        //if (!inCache()) {
        {
            //auto scopedTimings = g_stats.timings.svdagTraversalTime.getScopedStopwatch();

            auto& [svdag, svdagRayOffset] = m_svdagAndTransform;
            auto svdagRay = ray;
            svdagRay.origin = glm::vec3(1.0f) + (svdagRayOffset.invGridBoundsExtent * (ray.origin - svdagRayOffset.gridBoundsMin));
            auto tOpt = svdag.intersectScalar(svdagRay);
            if (!tOpt)
                return false; // Missed, continue traversal
        }
    }

    auto* mutThisPtr = const_cast<TopLevelLeafNode*>(this);

    RayBatch* batch = mutThisPtr->m_threadLocalActiveBatch.local();
    if (!batch || batch->full()) {
        if (batch) {
            // Batch was full, move it to the list of immutable batches
            auto* oldHead = mutThisPtr->m_immutableRayBatchList.load();
            do {
                batch->setNext(oldHead);
            } while (!mutThisPtr->m_immutableRayBatchList.compare_exchange_weak(oldHead, batch));

            mutThisPtr->m_numFullBatches.fetch_add(1);
        }

        // Allocate a new batch and set it as the new active batch
        auto* mem = mutThisPtr->m_accelerationStructurePtr->m_batchAllocator.allocate();
        batch = new (mem) RayBatch();
        mutThisPtr->m_threadLocalActiveBatch.local() = batch;
    }

    bool success = batch->tryPush(ray, userState, insertHandle);
    assert(success);

    return {}; // Paused
}

template <typename UserState, size_t BatchSize>
inline bool OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::forwardPartiallyFilledBatches()
{
    bool forwardedBatches = false;

    OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch* outBatch = m_immutableRayBatchList;
    int forwardedRays = 0;
    for (auto& batch : m_threadLocalActiveBatch) {
        if (batch) {
            for (const auto& [ray, hitInfoOpt, userState, insertHandle] : *batch) {
                if (!outBatch || outBatch->full()) {
                    auto* mem = m_accelerationStructurePtr->m_batchAllocator.allocate();
                    auto* newBatch = new (mem) RayBatch();

                    newBatch->setNext(outBatch);
                    outBatch = newBatch;
                    m_numFullBatches.fetch_add(1);
                }

                if (hitInfoOpt) {
                    outBatch->tryPush(ray, *hitInfoOpt, userState, insertHandle);
                } else {
                    outBatch->tryPush(ray, userState, insertHandle);
                }
                forwardedRays++;
            }

            m_accelerationStructurePtr->m_batchAllocator.deallocate(batch);
            forwardedBatches = true;
        }
        batch = nullptr; // Reset thread-local batch
    }
    m_immutableRayBatchList = outBatch;

    return forwardedBatches;
}

template <typename UserState, size_t BatchSize>
inline void OOCBatchingAccelerationStructure<UserState, BatchSize>::flush()
{
    int i = 0;
    while (true) {
        std::cout << "FLUSHING " << (i++) << std::endl;
        TopLevelLeafNode::flushRange(m_bvh.leafs(), this);

        bool done = true;
        for (auto* node : m_bvh.leafs()) {
            done = done && !node->hasFullBatches();

            // Put this in a separate variable doing the following will only execute when done is false:
            // done = done && !forwardedBatches()
            bool forwardedBatches = node->forwardPartiallyFilledBatches();
            done = done && !forwardedBatches;
        }

        if (done)
            break;
    }

    std::cout << "FLUSH COMPLETE" << std::endl;
}

template <typename UserState, size_t BatchSize>
void OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::flushRange(
    gsl::span<TopLevelLeafNode*> nodes,
    OOCBatchingAccelerationStructure<UserState, BatchSize>* accelerationStructurePtr)
{
    const size_t hwConcurrency = std::thread::hardware_concurrency();
    const size_t cacheConcurrency = accelerationStructurePtr->m_numLoadingThreads;
    const size_t flowConcurrency = 2 * cacheConcurrency;

    std::vector<TopLevelLeafNode*> cachedNodes;
    std::vector<TopLevelLeafNode*> nonCachedNodes;
    std::copy_if(std::begin(nodes), std::end(nodes), std::back_inserter(cachedNodes), [](TopLevelLeafNode* n) {
        return n->hasFullBatches() && n->inCache();
    });
    std::copy_if(std::begin(nodes), std::end(nodes), std::back_inserter(nonCachedNodes), [](TopLevelLeafNode* n) {
        return n->hasFullBatches() && !n->inCache();
    });

    std::cout << "Cached nodes: " << cachedNodes.size() << std::endl;
    std::cout << "Non cached nodes: " << nonCachedNodes.size() << std::endl;

    std::sort(std::begin(cachedNodes), std::end(cachedNodes), [](const auto* node1, const auto* node2) {
        return node1->m_numFullBatches.load() > node2->m_numFullBatches.load(); // Sort from big to small
    });
    std::sort(std::begin(nonCachedNodes), std::end(nonCachedNodes), [](const auto* node1, const auto* node2) {
        return node1->m_numFullBatches.load() > node2->m_numFullBatches.load(); // Sort from big to small
    });

    // Only flush nodes that have a lot of flushed batches, wait for other nodes for their batches to fill up.
    const int maxFullBatchesCached = cachedNodes.empty() ? 0 : cachedNodes[0]->m_numFullBatches.load();
    const int maxFullBatchesNonCached = nonCachedNodes.empty() ? 0 : nonCachedNodes[0]->m_numFullBatches.load();
    //const int batchesThreshold = std::max(maxFullBatchesCached, maxFullBatchesNonCached) / 8;
    const int batchesThreshold = maxFullBatchesNonCached / 8;

    tbb::flow::graph g;

    // Generate a task for each top-level leaf node that is in cache OR has enough full batches
    static constexpr float traversalCostPerRay = 1.0f; // Magic number based on CPU IPC & clock speed (ignore core count)
    static constexpr float diskSpeedBytesPerSecond = 300.0f * 1000000.0f; // Disk speed in b/s
    std::atomic_int traversalMargin = 0;

    std::atomic_size_t cachedNodeIndex = 0; // source_node is always run sequentially
    std::atomic_size_t nonCachedNodeIndex = 0;
    using BatchWithoutGeom = std::pair<RayBatch*, EvictableResourceID>;
    tbb::flow::source_node<BatchWithoutGeom> sourceNode(
        g,
        [&](BatchWithoutGeom& out) -> bool {
            TopLevelLeafNode* node = nullptr;

            /*const bool canDoNonCached = (nonCachedNodeIndex < nonCachedNodes.size() &&
                nonCachedNodes[nonCachedNodeIndex]->m_numFullBatches.load() >= batchesThreshold);
            if ((traversalMargin <= 0 || !canDoNonCached) && cachedNodeIndex < cachedNodes.size()) {
                node = cachedNodes[cachedNodeIndex.fetch_add(1)];
                const float traversalCost = node->m_numFullBatches.load() * BatchSize * traversalCostPerRay / hwConcurrency;
                traversalMargin.fetch_add(static_cast<int>(traversalCost));
            } else if (canDoNonCached) {
                node = nonCachedNodes[nonCachedNodeIndex.fetch_add(1)];
                const float loadCost = node->diskSizeBytes() / diskSpeedBytesPerSecond;
                const float traversalCost = node->m_numFullBatches.load() * BatchSize * traversalCostPerRay / hwConcurrency;
                const int nettoCost = static_cast<int>(loadCost - traversalCost);
                traversalMargin.fetch_sub(nettoCost);
            } else {
                return false;
            }*/

            if (cachedNodeIndex != cachedNodes.size()) {
                // Process nodes that are in cache first
                node = cachedNodes[cachedNodeIndex.fetch_add(1)];
            } else {
                // Then process nodes that were not in cache AND have enough full batches
                while (true) {
                    // All nodes processed
                    if (nonCachedNodeIndex == nonCachedNodes.size()) {
                        return false;
                    }

                    node = nonCachedNodes[nonCachedNodeIndex.fetch_add(1)];
                    if (node->m_numFullBatches.load() >= batchesThreshold) {
                        break;
                    }
                }
            }

            RayBatch* batch = node->m_immutableRayBatchList.exchange(nullptr);
            node->m_numFullBatches.store(0);
            out = { batch, node->m_geometryDataCacheID };
            return true;
        });

    // Prevent TBB from loading all items from the cache before starting traversal
    tbb::flow::limiter_node<BatchWithoutGeom> flowLimiterNode(g, flowConcurrency);

    // For each of those leaf nodes, load the geometry (asynchronously)
    auto cacheSubGraph = std::move(accelerationStructurePtr->m_geometryCache.template getFlowGraphNode<RayBatch*, CachedBatchingPoint>(g));

    // Create a task for each batch associated with that leaf node (for increased parallelism)
    using BatchWithGeom = std::pair<RayBatch*, std::shared_ptr<CachedBatchingPoint>>;
    using BatchWithGeomLimited = std::pair<std::pair<std::shared_ptr<std::atomic_int>, RayBatch*>, std::shared_ptr<CachedBatchingPoint>>;
    using BatchNodeType = tbb::flow::multifunction_node<BatchWithGeom, tbb::flow::tuple<BatchWithGeomLimited>>;
    BatchNodeType batchNode(
        g,
        tbb::flow::unlimited,
        [&](const BatchWithGeom& v, typename BatchNodeType::output_ports_type& op) {
            auto [firstBatch, geometry] = v;

            // Count the number of batches
            int numBatches = 0;
            const auto* tmpBatch = firstBatch;
            while (tmpBatch) {
                numBatches++;
                tmpBatch = tmpBatch->next();
            }
            if (numBatches == 0) {
                return;
            }

            // Flow limitter
            auto unprocessedBatchesCounter = std::make_shared<std::atomic_int>(numBatches);
            size_t raysProcessed = 0;
            auto* batch = firstBatch;
            while (batch) {
                bool success = std::get<0>(op).try_put({ { unprocessedBatchesCounter, batch }, geometry });
                assert(success);
                batch = batch->next();
            }
        });

    using TraverseNodeType = tbb::flow::multifunction_node<BatchWithGeomLimited, tbb::flow::tuple<tbb::flow::continue_msg>>;
    TraverseNodeType traversalNode(
        g,
        tbb::flow::unlimited,
        [&](const BatchWithGeomLimited& v, typename TraverseNodeType::output_ports_type& op) {
            auto [batchInfo, geometryData] = v;
            auto [unprocessedBatchCounter, batch] = batchInfo;

            for (auto [ray, rayHitOpt, userState, insertHandle] : *batch) {
                // Intersect with the bottom-level BVH
                if (rayHitOpt) {
                    RayHit& rayHit = *rayHitOpt;
                    geometryData->leafBVH.intersect(gsl::make_span(&ray, 1), gsl::make_span(&rayHit, 1));
                } else {
                    geometryData->leafBVH.intersectAny(gsl::make_span(&ray, 1));
                }
            }

            for (auto [ray, rayHitOpt, userState, insertHandle] : *batch) {
                if (rayHitOpt) {
                    // Insert the ray back into the top-level  BVH
                    auto optResult = accelerationStructurePtr->m_bvh.intersect(ray, *rayHitOpt, userState, insertHandle);
                    if (optResult && *optResult == false) {
                        // Ray exited the system so we can run the hit/miss shaders
                        if (rayHitOpt->primitiveID != -1) {
                            const auto& hitSceneObjectInfo = std::get<RayHit::OutOfCore>(rayHitOpt->sceneObjectVariant);
                            auto si = hitSceneObjectInfo.sceneObjectGeometry->fillSurfaceInteraction(ray, *rayHitOpt);
                            si.sceneObject = hitSceneObjectInfo.sceneObject;
                            auto material = hitSceneObjectInfo.sceneObject->getMaterialBlocking();
                            si.sceneObjectMaterial = material.get();
                            accelerationStructurePtr->m_hitCallback(ray, si, userState);
                        } else {
                            accelerationStructurePtr->m_missCallback(ray, userState);
                        }
                    }
                } else {
                    // Intersect any
                    if (ray.tfar == -std::numeric_limits<float>::infinity()) { // Ray hit something
                        accelerationStructurePtr->m_anyHitCallback(ray, userState);
                    } else {
                        auto optResult = accelerationStructurePtr->m_bvh.intersectAny(ray, userState, insertHandle);
                        if (optResult && *optResult == false) {
                            // Ray exited system
                            accelerationStructurePtr->m_missCallback(ray, userState);
                        }
                    }
                }
            }

            if (unprocessedBatchCounter->fetch_sub(1) == 1) {
                auto success = std::get<0>(op).try_put(tbb::flow::continue_msg());
                assert(success);
            }

            accelerationStructurePtr->m_batchAllocator.deallocate(batch);
        });

    // NOTE: The source starts outputting as soon as an edge is connected.
    //       So make sure it is the last edge that we connect.
    cacheSubGraph.connectInput(flowLimiterNode);
    cacheSubGraph.connectOutput(batchNode);
    tbb::flow::make_edge(batchNode, traversalNode);
    tbb::flow::make_edge(tbb::flow::output_port<0>(traversalNode), flowLimiterNode.decrement);
    tbb::flow::make_edge(sourceNode, flowLimiterNode);
    g.wait_for_all();
}

template <typename UserState, size_t BatchSize>
inline void OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::compressSVDAGs(gsl::span<TopLevelLeafNode*> nodes)
{
    std::vector<SparseVoxelDAG*> dags;
    for (auto* node : nodes) {
        dags.push_back(&std::get<0>(node->m_svdagAndTransform));
    }
    SparseVoxelDAG::compressDAGs(dags);

    for (const auto* dag : dags) {
        g_stats.memory.svdags += dag->sizeBytes();
    }
}

template <typename UserState, size_t BatchSize>
inline size_t OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::sizeBytes() const
{
    size_t size = sizeof(decltype(*this));
    size += m_sceneObjects.capacity() * sizeof(const OOCSceneObject*);
    size += m_threadLocalActiveBatch.size() * sizeof(RayBatch*);
    size += std::get<0>(m_svdagAndTransform).sizeBytes();
    return size;
}

template <typename UserState, size_t BatchSize>
inline size_t OOCBatchingAccelerationStructure<UserState, BatchSize>::TopLevelLeafNode::diskSizeBytes() const
{
    return m_diskSize;
}

template <typename UserState, size_t BatchSize>
inline OOCBatchingAccelerationStructure<UserState, BatchSize>::BotLevelLeafNodeInstanced::BotLevelLeafNodeInstanced(
    const SceneObjectGeometry* baseSceneObjectGeometry,
    unsigned primitiveID)
    : m_baseSceneObjectGeometry(baseSceneObjectGeometry)
    , m_primitiveID(primitiveID)
{
}

template <typename UserState, size_t BatchSize>
inline Bounds OOCBatchingAccelerationStructure<UserState, BatchSize>::BotLevelLeafNodeInstanced::getBounds() const
{
    return m_baseSceneObjectGeometry->worldBoundsPrimitive(m_primitiveID);
}

template <typename UserState, size_t BatchSize>
inline bool OOCBatchingAccelerationStructure<UserState, BatchSize>::BotLevelLeafNodeInstanced::intersect(Ray& ray, RayHit& hitInfo) const
{
    auto ret = m_baseSceneObjectGeometry->intersectPrimitive(ray, hitInfo, m_primitiveID);
    return ret;
}

template <typename UserState, size_t BatchSize>
inline OOCBatchingAccelerationStructure<UserState, BatchSize>::BotLevelLeafNode::BotLevelLeafNode(
    const OOCGeometricSceneObject* sceneObject,
    const std::shared_ptr<SceneObjectGeometry>& sceneObjectGeometry,
    unsigned primitiveID)
    : m_data(Regular { sceneObject, sceneObjectGeometry, primitiveID })
{
}

template <typename UserState, size_t BatchSize>
inline OOCBatchingAccelerationStructure<UserState, BatchSize>::BotLevelLeafNode::BotLevelLeafNode(
    const OOCInstancedSceneObject* sceneObject,
    const std::shared_ptr<SceneObjectGeometry>& sceneObjectGeometry,
    const std::shared_ptr<WiVeBVH8Build8<OOCBatchingAccelerationStructure<UserState, BatchSize>::BotLevelLeafNodeInstanced>>& bvh)
    : m_data(Instance { sceneObject, sceneObjectGeometry, bvh })
{
}

template <typename UserState, size_t BatchSize>
inline Bounds OOCBatchingAccelerationStructure<UserState, BatchSize>::BotLevelLeafNode::getBounds() const
{
    if (std::holds_alternative<Regular>(m_data)) {
        const auto& data = std::get<Regular>(m_data);
        return data.sceneObjectGeometry->worldBoundsPrimitive(data.primitiveID);
    } else {
        return std::get<Instance>(m_data).sceneObject->worldBounds();
    }
}

template <typename UserState, size_t BatchSize>
inline bool OOCBatchingAccelerationStructure<UserState, BatchSize>::BotLevelLeafNode::intersect(Ray& ray, RayHit& hitInfo) const
{
    if (std::holds_alternative<Regular>(m_data)) {
        const auto& data = std::get<Regular>(m_data);

        bool hit = data.sceneObjectGeometry->intersectPrimitive(ray, hitInfo, data.primitiveID);
        if (hit) {
            hitInfo.sceneObjectVariant = RayHit::OutOfCore { data.sceneObject, data.sceneObjectGeometry };
        }
        return hit;
    } else {
        const auto& data = std::get<Instance>(m_data);

        RayHit localHitInfo;
        Ray localRay = data.sceneObject->transformRayToInstanceSpace(ray);
        data.bvh->intersect(gsl::make_span(&localRay, 1), gsl::make_span(&localHitInfo, 1));
        if (localHitInfo.primitiveID != -1) {
            ray.tfar = localRay.tfar;
            hitInfo = localHitInfo;
            hitInfo.sceneObjectVariant = RayHit::OutOfCore { data.sceneObject, data.sceneObjectGeometry };
            return true;
        } else {
            return false;
        }
    }
}

template <typename UserState, size_t BatchSize>
inline bool OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::tryPush(const Ray& ray, const RayHit& rayHit, const UserState& state, const PauseableBVHInsertHandle& insertHandle)
{
    m_data.emplace_back(ray, rayHit, state, insertHandle);
    return true;
}

template <typename UserState, size_t BatchSize>
inline bool OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::tryPush(const Ray& ray, const UserState& state, const PauseableBVHInsertHandle& insertHandle)
{
    m_data.emplace_back(ray, state, insertHandle);
    return true;
}

template <typename UserState, size_t BatchSize>
inline const typename OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::begin()
{
    return iterator(this, 0);
}

template <typename UserState, size_t BatchSize>
inline const typename OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::end()
{
    return iterator(this, m_data.size());
}

template <typename UserState, size_t BatchSize>
inline OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator::iterator(OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch* batch, size_t index)
    : m_rayBatch(batch)
    , m_index(index)
{
}

template <typename UserState, size_t BatchSize>
inline typename OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator& OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator::operator++()
{
    m_index++;
    return *this;
}

template <typename UserState, size_t BatchSize>
inline typename OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator::operator++(int)
{
    auto r = *this;
    m_index++;
    return r;
}

template <typename UserState, size_t BatchSize>
inline bool OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator::operator==(OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator other) const
{
    assert(m_rayBatch == other.m_rayBatch);
    return m_index == other.m_index;
}

template <typename UserState, size_t BatchSize>
inline bool OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator::operator!=(OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator other) const
{
    assert(m_rayBatch == other.m_rayBatch);
    return m_index != other.m_index;
}

template <typename UserState, size_t BatchSize>
inline typename OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator::value_type OOCBatchingAccelerationStructure<UserState, BatchSize>::RayBatch::iterator::operator*()
{
    auto& [ray, rayHitOpt, userState, insertHandle] = m_rayBatch->m_data[m_index];
    return { ray, rayHitOpt, userState, insertHandle };
}
}
