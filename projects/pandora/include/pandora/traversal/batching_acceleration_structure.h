#pragma once
#include "pandora/graphics_core/bounds.h"
#include "pandora/graphics_core/pandora.h"
#include "pandora/graphics_core/scene.h"
#include "pandora/graphics_core/shape.h"
#include "pandora/samplers/rng/pcg.h"
#include "pandora/traversal/acceleration_structure.h"
#include "pandora/traversal/embree_cache.h"
#include "pandora/traversal/pauseable_bvh/pauseable_bvh4.h"
#include "stream/cache/lru_cache.h"
#include "stream/task_graph.h"
#include <embree3/rtcore.h>
#include <glm/gtc/type_ptr.hpp>
#include <gsl/span>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace pandora {

class BatchingAccelerationStructureBuilder;

template <typename HitRayState, typename AnyHitRayState>
class BatchingAccelerationStructure : public AccelerationStructure<HitRayState, AnyHitRayState> {
public:
    ~BatchingAccelerationStructure();

    void intersect(const Ray& ray, const HitRayState& state) const;
    void intersectAny(const Ray& ray, const AnyHitRayState& state) const;

    std::optional<SurfaceInteraction> intersectDebug(Ray& ray) const;

private:
    friend class BatchingAccelerationStructureBuilder;
    class BatchingPoint;
    BatchingAccelerationStructure(
        RTCDevice embreeDevice, PauseableBVH4<BatchingPoint, HitRayState, AnyHitRayState>&& topLevelBVH,
        tasking::TaskHandle<std::tuple<Ray, SurfaceInteraction, HitRayState>> hitTask, tasking::TaskHandle<std::tuple<Ray, HitRayState>> missTask,
        tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>> anyHitTask, tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>> anyMissTask,
        tasking::LRUCache* pGeometryCache, tasking::TaskGraph* pTaskGraph);

    using TopLevelBVH = PauseableBVH4<BatchingPoint, HitRayState, AnyHitRayState>;
    using OnHitTask = tasking::TaskHandle<std::tuple<Ray, SurfaceInteraction, HitRayState>>;
    using OnMissTask = tasking::TaskHandle<std::tuple<Ray, HitRayState>>;
    using OnAnyHitTask = tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>>;
    using OnAnyMissTask = tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>>;

    class BatchingPoint {
    public:
        BatchingPoint(SubScene&& subScene, tasking::LRUCache* pGeometryCache, tasking::TaskGraph* pTaskGraph);

        std::optional<bool> intersect(Ray&, SurfaceInteraction&, const HitRayState&, const PauseableBVHInsertHandle&) const;
        std::optional<bool> intersectAny(Ray&, const AnyHitRayState&, const PauseableBVHInsertHandle&) const;

        Bounds getBounds() const;

    private:
        bool intersectInternal(RTCScene scene, Ray&, SurfaceInteraction&) const;
        bool intersectAnyInternal(RTCScene scene, Ray&) const;

        friend class BatchingAccelerationStructure<HitRayState, AnyHitRayState>;
        void setParent(BatchingAccelerationStructure<HitRayState, AnyHitRayState>* pParent, EmbreeSceneCache* pEmbreeCache);

        static RTCScene buildEmbreeBVH(const SubScene& subScene, tasking::LRUCache* pCache, RTCDevice embreeDevice, std::unordered_map<const SceneNode*, RTCScene>& sceneCache);
        static RTCScene buildSubTreeEmbreeBVH(const SceneNode* pSceneNode, tasking::LRUCache* pCache, RTCDevice embreeDevice, std::unordered_map<const SceneNode*, RTCScene>& sceneCache);

        struct StaticData {
            std::vector<tasking::CachedPtr<Shape>> shapeOwners;

            std::shared_ptr<CachedEmbreeScene> scene;
        };

    private:
        SubScene m_subScene;
        Bounds m_bounds;
        //RTCScene m_embreeSubScene;
        glm::vec3 m_color;

        //BatchingAccelerationStructure* m_pParent;
        tasking::LRUCache* m_pGeometryCache;
        EmbreeSceneCache* m_pEmbreeCache;
        tasking::TaskGraph* m_pTaskGraph;

        tasking::TaskHandle<std::tuple<Ray, SurfaceInteraction, HitRayState, PauseableBVHInsertHandle>> m_intersectTask;
        tasking::TaskHandle<std::tuple<Ray, AnyHitRayState, PauseableBVHInsertHandle>> m_intersectAnyTask;
    };

private:
    RTCDevice m_embreeDevice;
    TopLevelBVH m_topLevelBVH;
    LRUEmbreeSceneCache m_embreeSceneCache;

    tasking::TaskGraph* m_pTaskGraph;

    tasking::TaskHandle<std::tuple<Ray, SurfaceInteraction, HitRayState>> m_onHitTask;
    tasking::TaskHandle<std::tuple<Ray, HitRayState>> m_onMissTask;
    tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>> m_onAnyHitTask;
    tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>> m_onAnyMissTask;
};

class BatchingAccelerationStructureBuilder {
public:
    BatchingAccelerationStructureBuilder(
        const Scene* pScene, tasking::LRUCache* pCache, tasking::TaskGraph* pTaskGraph, unsigned primitivesPerBatchingPoint);

    static void preprocessScene(Scene& scene, tasking::LRUCache& oldCache, tasking::CacheBuilder& newCacheBuilder, unsigned primitivesPerBatchingPoint);

    template <typename HitRayState, typename AnyHitRayState>
    BatchingAccelerationStructure<HitRayState, AnyHitRayState> build(
        tasking::TaskHandle<std::tuple<Ray, SurfaceInteraction, HitRayState>> hitTask, tasking::TaskHandle<std::tuple<Ray, HitRayState>> missTask,
        tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>> anyHitTask, tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>> anyMissTask);

private:
    static void splitLargeSceneObjectsRecurse(SceneNode* pNode, tasking::LRUCache& oldCache, tasking::CacheBuilder& newCacheBuilder, RTCDevice embreeDevice, unsigned maxSize);
    static void verifyInstanceDepth(const SceneNode* pSceneNode, int depth = 0);

private:
    RTCDevice m_embreeDevice;
    std::vector<SubScene> m_subScenes;

    tasking::LRUCache* m_pGeometryCache;
    tasking::TaskGraph* m_pTaskGraph;
};

inline glm::vec3 randomVec3()
{
    static PcgRng rng { 94892380 };
    return rng.uniformFloat3();
}

template <typename HitRayState, typename AnyHitRayState>
BatchingAccelerationStructure<HitRayState, AnyHitRayState>::BatchingPoint::BatchingPoint(
    SubScene&& subScene, tasking::LRUCache* pGeometryCache, tasking::TaskGraph* pTaskGraph)
    : m_subScene(std::move(subScene))
    , m_bounds(m_subScene.computeBounds())
    , m_color(randomVec3())
    , m_pGeometryCache(pGeometryCache)
    , m_pTaskGraph(pTaskGraph)
{
}

template <typename HitRayState, typename AnyHitRayState>
void BatchingAccelerationStructure<HitRayState, AnyHitRayState>::BatchingPoint::setParent(
    BatchingAccelerationStructure<HitRayState, AnyHitRayState>* pParent, EmbreeSceneCache* pEmbreeCache)
{
    //m_pParent = pParent;
    m_intersectTask = m_pTaskGraph->addTask<std::tuple<Ray, SurfaceInteraction, HitRayState, PauseableBVHInsertHandle>, StaticData>(
        [=]() -> StaticData {
            StaticData staticData;

            // Load all top level shapes
            for (const auto& pSceneObject : m_subScene.sceneObjects) {
                auto shapeOwner = m_pGeometryCache->makeResident(pSceneObject->pShape.get());
                staticData.shapeOwners.emplace_back(std::move(shapeOwner));
            }

            // Load instanced shapes
            std::function<void(const SceneNode*)> makeResidentRecurse = [&](const SceneNode* pSceneNode) {
                for (const auto& pSceneObject : pSceneNode->objects) {
                    auto shapeOwner = m_pGeometryCache->makeResident(pSceneObject->pShape.get());
                    staticData.shapeOwners.emplace_back(std::move(shapeOwner));
                }
                for (const auto& [pChild, _] : pSceneNode->children) {
                    makeResidentRecurse(pChild.get());
                }
            };

            staticData.scene = pEmbreeCache->fromSubScene(&m_subScene);

            return staticData;
        },
        [=](gsl::span<std::tuple<Ray, SurfaceInteraction, HitRayState, PauseableBVHInsertHandle>> data, const StaticData* pStaticData, std::pmr::memory_resource* pMemoryResource) {
            RTCScene embreeScene = pStaticData->scene->scene;
            for (auto& [ray, si, state, insertHandle] : data) {
                intersectInternal(embreeScene, ray, si);
            }

            for (auto& [ray, si, state, insertHandle] : data) {
                auto optHit = pParent->m_topLevelBVH.intersect(ray, si, state, insertHandle);
                if (optHit) { // Ray exited BVH
                    assert(!optHit.value());

                    if (si.pSceneObject) {
                        // Ray hit something
                        assert(si.pSceneObject);
                        m_pTaskGraph->enqueue(pParent->m_onHitTask, std::tuple { ray, si, state });
                    } else {
                        m_pTaskGraph->enqueue(pParent->m_onMissTask, std::tuple { ray, state });
                    }
                }
            }
        });
    m_intersectAnyTask = m_pTaskGraph->addTask<std::tuple<Ray, AnyHitRayState, PauseableBVHInsertHandle>>(
        [=](gsl::span<std::tuple<Ray, AnyHitRayState, PauseableBVHInsertHandle>> data, std::pmr::memory_resource* pMemoryResource) {
            std::vector<uint32_t> hits;
            hits.resize(data.size());

            int i = 0;
            for (auto& [ray, state, insertHandle] : data) {
                //hits[i++] = intersectAnyInternal(ray);
            }

            for (auto& [ray, state, insertHandle] : data) {
                bool hit = hits[i++];

                if (hit) {
                    m_pTaskGraph->enqueue(pParent->m_onAnyHitTask, std::tuple { ray, state });
                } else {
                    auto optHit = pParent->m_topLevelBVH.intersectAny(ray, state, insertHandle);
                    if (optHit) { // Ray exited BVH
                        assert(!optHit.value());
                        m_pTaskGraph->enqueue(pParent->m_onAnyMissTask, std::tuple { ray, state });
                    } else {
                        // Nodes should always be paused, only way to return is when ray exists BVH
                        throw std::runtime_error("Invalid code path");
                    }
                }
            }
        });
}

template <typename HitRayState, typename AnyHitRayState>
Bounds BatchingAccelerationStructure<HitRayState, AnyHitRayState>::BatchingPoint::getBounds() const
{
    return m_bounds;
}

template <typename HitRayState, typename AnyHitRayState>
inline std::optional<SurfaceInteraction> BatchingAccelerationStructure<HitRayState, AnyHitRayState>::intersectDebug(Ray& ray) const
{
    SurfaceInteraction si;
    if (m_topLevelBVH.intersect(ray, si, HitRayState {}))
        return si;
    else
        return {};
}

template <typename HitRayState, typename AnyHitRayState>
std::optional<bool> BatchingAccelerationStructure<HitRayState, AnyHitRayState>::BatchingPoint::intersect(
    Ray& ray, SurfaceInteraction& si, const HitRayState& userState, const PauseableBVHInsertHandle& bvhInsertHandle) const
{
    m_pTaskGraph->enqueue(m_intersectTask, std::tuple { ray, si, userState, bvhInsertHandle });
    return {};
}

template <typename HitRayState, typename AnyHitRayState>
std::optional<bool> BatchingAccelerationStructure<HitRayState, AnyHitRayState>::BatchingPoint::intersectAny(
    Ray& ray, const AnyHitRayState& userState, const PauseableBVHInsertHandle& bvhInsertHandle) const
{
    m_pTaskGraph->enqueue(m_intersectAnyTask, std::tuple { ray, userState, bvhInsertHandle });
    return {};
}

template <typename HitRayState, typename AnyHitRayState>
bool BatchingAccelerationStructure<HitRayState, AnyHitRayState>::BatchingPoint::intersectInternal(
    RTCScene scene, Ray& ray, SurfaceInteraction& si) const
{
    // TODO: batching
    RTCIntersectContext context {};

    RTCRayHit embreeRayHit;
    embreeRayHit.ray.org_x = ray.origin.x;
    embreeRayHit.ray.org_y = ray.origin.y;
    embreeRayHit.ray.org_z = ray.origin.z;
    embreeRayHit.ray.dir_x = ray.direction.x;
    embreeRayHit.ray.dir_y = ray.direction.y;
    embreeRayHit.ray.dir_z = ray.direction.z;

    embreeRayHit.ray.tnear = ray.tnear;
    embreeRayHit.ray.tfar = ray.tfar;

    embreeRayHit.ray.time = 0.0f;
    embreeRayHit.ray.mask = 0xFFFFFFFF;
    embreeRayHit.ray.id = 0;
    embreeRayHit.ray.flags = 0;
    embreeRayHit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rtcIntersect1(scene, &context, &embreeRayHit);

    if (embreeRayHit.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
        std::optional<glm::mat4> transform;
        const SceneObject* pSceneObject { nullptr };

        if (embreeRayHit.hit.instID[0] == 0) {
            pSceneObject = reinterpret_cast<const SceneObject*>(
                rtcGetGeometryUserData(rtcGetGeometry(scene, embreeRayHit.hit.geomID)));
        } else {
            glm::mat4 accumulatedTransform { 1.0f };
            RTCScene localScene = scene;
            for (int i = 0; i < RTC_MAX_INSTANCE_LEVEL_COUNT; i++) {
                unsigned geomID = embreeRayHit.hit.instID[i];
                if (geomID == 0)
                    break;

                RTCGeometry geometry = rtcGetGeometry(localScene, geomID);

                glm::mat4 localTransform;
                rtcGetGeometryTransform(geometry, 0.0f, RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR, glm::value_ptr(localTransform));
                accumulatedTransform *= localTransform;

                localScene = reinterpret_cast<RTCScene>(rtcGetGeometryUserData(geometry));
            }

            transform = accumulatedTransform;
            pSceneObject = reinterpret_cast<const SceneObject*>(
                rtcGetGeometryUserData(rtcGetGeometry(localScene, embreeRayHit.hit.geomID)));
        }

        RayHit hit;
        hit.geometricNormal = { embreeRayHit.hit.Ng_x, embreeRayHit.hit.Ng_y, embreeRayHit.hit.Ng_z };
        hit.geometricNormal = glm::normalize(glm::dot(-ray.direction, hit.geometricNormal) > 0.0f ? hit.geometricNormal : -hit.geometricNormal);
        hit.geometricUV = { embreeRayHit.hit.u, embreeRayHit.hit.v };
        hit.primitiveID = embreeRayHit.hit.primID;

        ray.tfar = embreeRayHit.ray.tfar;

        const auto* pShape = pSceneObject->pShape.get();
        si = pShape->fillSurfaceInteraction(ray, hit);
        si.pSceneObject = pSceneObject;
        si.localToWorld = transform;
        si.shading.batchingPointColor = m_color;
        return true;
    } else {
        return false;
    }
}

template <typename HitRayState, typename AnyHitRayState>
bool BatchingAccelerationStructure<HitRayState, AnyHitRayState>::BatchingPoint::intersectAnyInternal(
    RTCScene scene, Ray& ray) const
{
    // TODO: batching
    RTCIntersectContext context {};

    RTCRay embreeRay;
    embreeRay.org_x = ray.origin.x;
    embreeRay.org_y = ray.origin.y;
    embreeRay.org_z = ray.origin.z;
    embreeRay.dir_x = ray.direction.x;
    embreeRay.dir_y = ray.direction.y;
    embreeRay.dir_z = ray.direction.z;

    embreeRay.tnear = ray.tnear;
    embreeRay.tfar = ray.tfar;

    embreeRay.time = 0.0f;
    embreeRay.mask = 0xFFFFFFFF;
    embreeRay.id = 0;
    embreeRay.flags = 0;

    rtcOccluded1(scene, &context, &embreeRay);

    ray.tfar = embreeRay.tfar;
    static constexpr float minInf = -std::numeric_limits<float>::infinity();
    return embreeRay.tfar != minInf;
}

template <typename HitRayState, typename AnyHitRayState>
inline BatchingAccelerationStructure<HitRayState, AnyHitRayState> BatchingAccelerationStructureBuilder::build(
    tasking::TaskHandle<std::tuple<Ray, SurfaceInteraction, HitRayState>> hitTask, tasking::TaskHandle<std::tuple<Ray, HitRayState>> missTask,
    tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>> anyHitTask, tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>> anyMissTask)
{
    spdlog::info("Creating batching points");

    std::unordered_map<const SceneNode*, RTCScene> embreeSceneCache;

    using BatchingPointT = typename BatchingAccelerationStructure<HitRayState, AnyHitRayState>::BatchingPoint;
    std::vector<BatchingPointT> batchingPoints;
    std::transform(std::begin(m_subScenes), std::end(m_subScenes), std::back_inserter(batchingPoints),
        [&](SubScene& subScene) {
            //verifyInstanceDepth(pSubSceneRoot.get());

            //std::unordered_map<const SceneNode*, RTCScene> sceneCache;
            //RTCScene embreeSubScene = buildEmbreeBVH(subScene, m_pGeometryCache, m_embreeDevice, embreeSceneCache);

            return BatchingPointT { std::move(subScene), m_pGeometryCache, m_pTaskGraph };
        });

    spdlog::info("Constructing top level BVH");

    // Moves batching points into internal structure
    PauseableBVH4<BatchingPointT, HitRayState, AnyHitRayState> topLevelBVH { batchingPoints };
    return BatchingAccelerationStructure<HitRayState, AnyHitRayState>(
        m_embreeDevice, std::move(topLevelBVH), hitTask, missTask, anyHitTask, anyMissTask, m_pGeometryCache, m_pTaskGraph);
}

template <typename HitRayState, typename AnyHitRayState>
inline BatchingAccelerationStructure<HitRayState, AnyHitRayState>::BatchingAccelerationStructure(
    RTCDevice embreeDevice, PauseableBVH4<BatchingPoint, HitRayState, AnyHitRayState>&& topLevelBVH,
    tasking::TaskHandle<std::tuple<Ray, SurfaceInteraction, HitRayState>> hitTask, tasking::TaskHandle<std::tuple<Ray, HitRayState>> missTask,
    tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>> anyHitTask, tasking::TaskHandle<std::tuple<Ray, AnyHitRayState>> anyMissTask,
    tasking::LRUCache* pGeometryCache, tasking::TaskGraph* pTaskGraph)
    : m_embreeDevice(embreeDevice)
    , m_topLevelBVH(std::move(topLevelBVH))
    , m_pTaskGraph(pTaskGraph)
    , m_onHitTask(hitTask)
    , m_onMissTask(missTask)
    , m_onAnyHitTask(anyHitTask)
    , m_onAnyMissTask(anyMissTask)
    , m_embreeSceneCache(20 * 1000 * 1000)
{
    for (auto& leaf : m_topLevelBVH.leafs())
        leaf.setParent(this, &m_embreeSceneCache);
}

template <typename HitRayState, typename AnyHitRayState>
inline BatchingAccelerationStructure<HitRayState, AnyHitRayState>::~BatchingAccelerationStructure()
{
    rtcReleaseDevice(m_embreeDevice);
}

template <typename HitRayState, typename AnyHitRayState>
inline void BatchingAccelerationStructure<HitRayState, AnyHitRayState>::intersect(const Ray& ray, const HitRayState& state) const
{
    auto mutRay = ray;
    SurfaceInteraction si;
    auto optHit = m_topLevelBVH.intersect(mutRay, si, state);
    if (optHit) {
        assert(!optHit.value());
        m_pTaskGraph->enqueue(m_onMissTask, std::tuple { mutRay, state });
    }
}

template <typename HitRayState, typename AnyHitRayState>
inline void BatchingAccelerationStructure<HitRayState, AnyHitRayState>::intersectAny(const Ray& ray, const AnyHitRayState& state) const
{
    auto mutRay = ray;
    auto optHit = m_topLevelBVH.intersectAny(mutRay, state);
    if (optHit) {
        assert(!optHit.value());
        m_pTaskGraph->enqueue(m_onAnyMissTask, std::tuple { mutRay, state });
    }
}

}