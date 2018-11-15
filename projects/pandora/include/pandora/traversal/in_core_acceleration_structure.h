#pragma once
#include "pandora/core/scene.h"
#include "pandora/core/stats.h"
#include "pandora/geometry/triangle.h"
#include "pandora/scene/geometric_scene_object.h"
#include "pandora/scene/instanced_scene_object.h"
#include "pandora/traversal/bvh/embree_bvh.h"
#include "pandora/traversal/bvh/naive_single_bvh2.h"
#include "pandora/traversal/bvh/wive_bvh8_build2.h"
#include "pandora/traversal/bvh/wive_bvh8_build8.h"
#include "pandora/traversal/pauseable_bvh/pauseable_bvh4.h"
#include "pandora/utility/memory_arena_ts.h"
#include <EASTL/fixed_vector.h>
#include <functional>
#include <gsl/gsl>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pandora {

template <typename UserState>
class InCoreAccelerationStructure {
public:
    constexpr static bool recurseTillCompletion = true; // Paths are recursively traced untill completion

    using InsertHandle = void*;
    using HitCallback = std::function<void(const Ray&, const SurfaceInteraction&, const UserState&)>;
    using AnyHitCallback = std::function<void(const Ray&, const UserState&)>;
    using MissCallback = std::function<void(const Ray&, const UserState&)>;

public:
    InCoreAccelerationStructure(const Scene& scene, HitCallback hitCallback, AnyHitCallback anyHitCallback, MissCallback missCallback);
    ~InCoreAccelerationStructure() = default;

    // Dummy
    bool placeIntersectRequestsReturnOnMiss(gsl::span<const Ray> rays, gsl::span<const UserState> perRayUserData) { return false; }

    void placeIntersectRequests(gsl::span<const Ray> rays, gsl::span<const UserState> perRayUserData, const InsertHandle& insertHandle = nullptr);
    void placeIntersectAnyRequests(gsl::span<const Ray> rays, gsl::span<const UserState> perRayUserData, const InsertHandle& insertHandle = nullptr);

    void flush(){}; // Dummy
private:
    template <typename T>
    using BVHType = WiVeBVH8Build8<T>;
    //using BVHType = EmbreeBVH<T>;
    //using BVHType = NaiveSingleRayBVH2<T>;

    // Leaf node of a bottom-level (instanced) BVH
    class InstanceLeafNode {
    public:
        InstanceLeafNode(const InCoreGeometricSceneObject* baseSceneObject, unsigned primitiveID);

        Bounds getBounds() const;
        bool intersect(Ray& ray, RayHit& hitInfo) const;

    private:
        // Is a primitive
        const InCoreGeometricSceneObject* m_baseSceneObject;
        const unsigned m_primitiveID;
    };

    class LeafNode {
    public:
        LeafNode(const InCoreSceneObject* sceneObject, const std::shared_ptr<BVHType<InstanceLeafNode>>& bvh); // Instanced object in the top-level BVH
        LeafNode(const InCoreSceneObject* sceneObject, unsigned primitiveID); // Leaf of the top-level BVH
        inline LeafNode(LeafNode&& other)
            : m_sceneObject(std::move(other.m_sceneObject))
            , m_primitiveID(std::move(other.m_primitiveID))
            , m_bvh(std::move(other.m_bvh))
        {
        }

        Bounds getBounds() const;
        bool intersect(Ray& ray, RayHit& hitInfo) const;

    private:
        // Is a primitive
        const InCoreSceneObject* m_sceneObject;
        const unsigned m_primitiveID;

        // Is an instance
        std::shared_ptr<BVHType<InstanceLeafNode>> m_bvh;
    };

    static BVHType<LeafNode> buildBVH(const Scene& scene);

private:
    BVHType<LeafNode> m_bvh;

    HitCallback m_hitCallback;
    AnyHitCallback m_anyHitCallback;
    MissCallback m_missCallback;
};

template <typename UserState>
inline InCoreAccelerationStructure<UserState>::InCoreAccelerationStructure(const Scene& scene, HitCallback hitCallback, AnyHitCallback anyHitCallback, MissCallback missCallback)
    : m_bvh(std::move(buildBVH(scene)))
    , m_hitCallback(hitCallback)
    , m_anyHitCallback(anyHitCallback)
    , m_missCallback(missCallback)
{
    //m_bvh.saveToFile("scene.bvh");
    //m_bvh.loadFromFile("scene.bvh", leafs);
}

template <typename UserState>
#ifdef _MSC_VER
inline typename InCoreAccelerationStructure<UserState>::BVHType<typename InCoreAccelerationStructure<UserState>::LeafNode>
#else
inline typename InCoreAccelerationStructure<UserState>::template BVHType<typename InCoreAccelerationStructure<UserState>::LeafNode>
#endif
InCoreAccelerationStructure<UserState>::buildBVH(const Scene& scene)
{
    auto sceneObjects = scene.getInCoreSceneObjects();

    // Instancing: find base scene objects
    std::unordered_set<const InCoreGeometricSceneObject*> instancingBaseSceneObjects;
    for (const auto& sceneObject : sceneObjects) {
        if (const auto* instancedSceneObject = dynamic_cast<const InCoreInstancedSceneObject*>(sceneObject)) {
            instancingBaseSceneObjects.insert(instancedSceneObject->getBaseObject());
        }
    }

    // Build BVHs for the base scene objects
    std::unordered_map<const InCoreGeometricSceneObject*, std::shared_ptr<BVHType<InstanceLeafNode>>> instancedBVHs;
    for (auto sceneObjectPtr : instancingBaseSceneObjects) {
        std::vector<InstanceLeafNode> leafs;
        for (unsigned primitiveID = 0; primitiveID < sceneObjectPtr->numPrimitives(); primitiveID++) {
            leafs.push_back(InstanceLeafNode(sceneObjectPtr, primitiveID));
        }

        instancedBVHs[sceneObjectPtr] = std::make_shared<BVHType<InstanceLeafNode>>(leafs);
    }

    // Build the final BVH over all unique primitives / instanced scene objects
    std::vector<LeafNode> leafs;
    for (const auto& sceneObject : sceneObjects) {
        if (const auto* instancedSceneObject = dynamic_cast<const InCoreInstancedSceneObject*>(sceneObject)) {
            ALWAYS_ASSERT(instancedBVHs.find(instancedSceneObject->getBaseObject()) != instancedBVHs.end());
            auto bvh = instancedBVHs[instancedSceneObject->getBaseObject()];
            leafs.emplace_back(sceneObject, bvh);
        } else {
            // Single occurence => primitives can be incorporated into the top level tree for better performance
            for (unsigned primitiveID = 0; primitiveID < sceneObject->numPrimitives(); primitiveID++) {
                leafs.emplace_back(sceneObject, primitiveID);
            }
        }
    }

    return BVHType<LeafNode>(leafs);
}

template <typename UserState>
inline void InCoreAccelerationStructure<UserState>::placeIntersectRequests(
    gsl::span<const Ray> rays,
    gsl::span<const UserState> perRayUserData,
    const InsertHandle& insertHandle)
{
    (void)insertHandle;
    assert(perRayUserData.size() == rays.size());

    constexpr int64_t rayPacketSize = 50;
    for (int i = 0; i < rays.size(); i += rayPacketSize) {
        int numRays = std::min(rayPacketSize, rays.size() - i);
        eastl::fixed_vector<RayHit, rayPacketSize> hitInfos(numRays);
        eastl::fixed_vector<Ray, rayPacketSize> mutRays(numRays);
        std::copy(std::begin(rays) + i, std::begin(rays) + i + numRays, std::begin(mutRays));

        m_bvh.intersect(mutRays, hitInfos);

        for (int j = 0; j < numRays; j++) {
            const auto& hitInfo = hitInfos[j];
            const auto& ray = mutRays[j];

            const auto* sceneObject = std::get<const InCoreSceneObject*>(hitInfo.sceneObjectVariant);
            if (sceneObject) {
                SurfaceInteraction si = sceneObject->fillSurfaceInteraction(ray, hitInfo);
                si.sceneObjectMaterial = sceneObject;
                m_hitCallback(ray, si, perRayUserData[i + j]);
            } else {
                m_missCallback(ray, perRayUserData[i + j]);
            }
        }
    }
}

template <typename UserState>
inline void InCoreAccelerationStructure<UserState>::placeIntersectAnyRequests(
    gsl::span<const Ray> rays,
    gsl::span<const UserState> perRayUserData,
    const InsertHandle& insertHandle)
{
    (void)insertHandle;
    assert(perRayUserData.size() == rays.size());

    constexpr int64_t rayPacketSize = 50;
    for (int i = 0; i < rays.size(); i += rayPacketSize) {
        int numRays = std::min(rayPacketSize, rays.size() - i);
        eastl::fixed_vector<Ray, rayPacketSize> mutRays(numRays);
        std::copy(std::begin(rays) + i, std::begin(rays) + i + numRays, std::begin(mutRays));

        m_bvh.intersectAny(mutRays);

        for (int j = 0; j < numRays; j++) {
            const auto& ray = mutRays[j];
            if (ray.tfar == -std::numeric_limits<float>::infinity()) {
                m_anyHitCallback(ray, perRayUserData[i + j]);
            } else {
                m_missCallback(ray, perRayUserData[i + j]);
            }
        }
    }
}

template <typename UserState>
inline InCoreAccelerationStructure<UserState>::InstanceLeafNode::InstanceLeafNode(
    const InCoreGeometricSceneObject* object,
    unsigned primitiveID)
    : m_baseSceneObject(object)
    , m_primitiveID(primitiveID)
{
}

template <typename UserState>
inline Bounds InCoreAccelerationStructure<UserState>::InstanceLeafNode::getBounds() const
{
    return m_baseSceneObject->worldBoundsPrimitive(m_primitiveID);
}

template <typename UserState>
inline bool InCoreAccelerationStructure<UserState>::InstanceLeafNode::intersect(Ray& ray, RayHit& hitInfo) const
{
    return m_baseSceneObject->intersectPrimitive(ray, hitInfo, m_primitiveID);
}

template <typename UserState>
inline InCoreAccelerationStructure<UserState>::LeafNode::LeafNode(
    const InCoreSceneObject* sceneObject,
    const std::shared_ptr<BVHType<InstanceLeafNode>>& bvh)
    : m_sceneObject(sceneObject)
    , m_primitiveID(0)
    , m_bvh(bvh)
{
    ALWAYS_ASSERT(m_bvh != nullptr);
}

template <typename UserState>
inline InCoreAccelerationStructure<UserState>::LeafNode::LeafNode(
    const InCoreSceneObject* sceneObject,
    unsigned primitiveID)
    : m_sceneObject(sceneObject)
    , m_primitiveID(primitiveID)
    , m_bvh(nullptr)
{
}

template <typename UserState>
inline Bounds InCoreAccelerationStructure<UserState>::LeafNode::getBounds() const
{
    ALWAYS_ASSERT(m_sceneObject != nullptr);

    if (m_bvh) {
        return m_sceneObject->worldBounds();
    } else {
        return m_sceneObject->worldBoundsPrimitive(m_primitiveID);
    }
}

template <typename UserState>
inline bool InCoreAccelerationStructure<UserState>::LeafNode::intersect(Ray& ray, RayHit& hitInfo) const
{
    if (!m_bvh) {
        bool hit = m_sceneObject->intersectPrimitive(ray, hitInfo, m_primitiveID);
        if (hit) {
            std::get<const InCoreSceneObject*>(hitInfo.sceneObjectVariant) = m_sceneObject;
        }
        return hit;
    } else {
        const auto* instancedSceneObject = dynamic_cast<const InCoreInstancedSceneObject*>(m_sceneObject);
        Ray localRay = instancedSceneObject->transformRayToInstanceSpace(ray);
        RayHit localRayHit;

        m_bvh->intersect(gsl::make_span(&localRay, 1), gsl::make_span(&localRayHit, 1));
        if (localRayHit.primitiveID != -1) {
            hitInfo = localRayHit;
            std::get<const InCoreSceneObject*>(hitInfo.sceneObjectVariant) = m_sceneObject;
            ray.tfar = localRay.tfar;
            return true;
        } else {
            return false;
        }
    }
}
}
