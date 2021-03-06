#include "pandora/traversal/batching.h"
#include "pandora/shapes/triangle.h"
#include "pandora/svo/voxel_grid.h"
#include "pandora/utility/enumerate.h"
#include "pandora/utility/error_handling.h"
#include "pandora/utility/math.h"
#include <mutex>
#include <optick.h>
#include <spdlog/spdlog.h>
#include <stream/cache/lru_cache.h>
#include <stream/cache/lru_cache_ts.h>
#include <tbb/task_group.h>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace pandora::detail {

static std::vector<std::shared_ptr<Shape>> splitLargeTriangleShape(const TriangleShape& original, unsigned maxSize, RTCDevice embreeDevice);
static void replaceShapeBySplitShapesRecurse(SceneNode* pSceneNode, tasking::LRUCacheTS& oldCache, tasking::CacheBuilder& newCacheBuilder, std::unordered_set<Shape*>& cachedShapes, const std::unordered_map<Shape*, std::vector<std::shared_ptr<Shape>>>& splitShapes);

static size_t subTreePrimitiveCount(const SceneNode* pSceneNode)
{
    size_t primCount = 0;
    for (const auto& pSceneObject : pSceneNode->objects) {
        primCount += static_cast<size_t>(pSceneObject->pShape->numPrimitives());
    }

    for (const auto& childAndTransform : pSceneNode->children) {
        const auto& [pChild, _] = childAndTransform;
        primCount += subTreePrimitiveCount(pChild.get());
    }
    return primCount;
}

std::vector<pandora::SubScene> createSubScenes(const pandora::Scene& scene, unsigned primitivesPerSubScene, RTCDevice embreeDevice)
{
    OPTICK_EVENT();

    // Split a large shape into smaller shapes with maxSize/2 to maxSize primitives.
    // The Embree BVH builder API used to efficiently partition the shapes primitives into groups.
    //using Path = eastl::fixed_vector<unsigned, 4>;
    using Path = std::vector<unsigned>;
    std::vector<RTCBuildPrimitive> embreeBuildPrimitives;

    for (const auto& [sceneObjectID, pSceneObject] : enumerate(scene.pRoot->objects)) {
        if (!pSceneObject->pShape)
            continue;

        const Bounds bounds = pSceneObject->pShape->getBounds();

        RTCBuildPrimitive primitive;
        primitive.lower_x = bounds.min.x;
        primitive.lower_y = bounds.min.y;
        primitive.lower_z = bounds.min.z;
        primitive.upper_x = bounds.max.x;
        primitive.upper_y = bounds.max.y;
        primitive.upper_z = bounds.max.z;
        primitive.geomID = 0;
        primitive.primID = sceneObjectID;
        embreeBuildPrimitives.push_back(primitive);
    }

    for (const auto& [subTreeID, childAndTransform] : enumerate(scene.pRoot->children)) {
        const auto& [pChild, transformOpt] = childAndTransform;
        Bounds bounds = pChild->computeBounds();
        if (transformOpt)
            bounds *= transformOpt.value();

        RTCBuildPrimitive primitive;
        primitive.lower_x = bounds.min.x;
        primitive.lower_y = bounds.min.y;
        primitive.lower_z = bounds.min.z;
        primitive.upper_x = bounds.max.x;
        primitive.upper_y = bounds.max.y;
        primitive.upper_z = bounds.max.z;
        primitive.geomID = 1;
        primitive.primID = subTreeID;
        embreeBuildPrimitives.push_back(primitive);
    }

    ALWAYS_ASSERT(embreeBuildPrimitives.size() < std::numeric_limits<unsigned>::max());

    // Build a BVH over all scene objects (including instanced ones)
    RTCBVH bvh = rtcNewBVH(embreeDevice);

    struct BVHNode {
        virtual ~BVHNode() {};

        eastl::fixed_vector<Bounds, 2, false> childBounds;
        //std::vector<Bounds> childBounds;
        size_t numPrimitives { 0 };
    };
    struct LeafNode : public BVHNode {
        //eastl::fixed_vector<std::pair<SceneNode*, std::optional<glm::mat4>>, 8, false> sceneNodes;
        std::vector<std::pair<SceneNode*, std::optional<glm::mat4>>> sceneNodes;
        std::vector<SceneObject*> sceneObjects;
    };
    struct InnerNode : public BVHNode {
        eastl::fixed_vector<BVHNode*, 2, false> children;
        //std::vector<BVHNode*> children;
    };

    RTCBuildArguments arguments = rtcDefaultBuildArguments();
    arguments.byteSize = sizeof(arguments);
    arguments.buildFlags = RTC_BUILD_FLAG_NONE;
    arguments.buildQuality = RTC_BUILD_QUALITY_MEDIUM; // High build quality requires spatial splits
    arguments.maxBranchingFactor = 2;
    arguments.minLeafSize = 2; // Stop splitting when number of prims is below minLeafSize
    arguments.maxLeafSize = 8; // This is a hard constraint (always split when number of prims is larger than maxLeafSize)
    arguments.maxDepth = 48;
    arguments.bvh = bvh;
    arguments.primitives = embreeBuildPrimitives.data();
    arguments.primitiveCount = embreeBuildPrimitives.size();
    arguments.primitiveArrayCapacity = embreeBuildPrimitives.size();
    arguments.userPtr = scene.pRoot.get();
    arguments.createNode = [](RTCThreadLocalAllocator alloc, unsigned numChildren, void*) -> void* {
        void* pMem = rtcThreadLocalAlloc(alloc, sizeof(InnerNode), std::alignment_of_v<InnerNode>);
        return static_cast<BVHNode*>(new (pMem) InnerNode());
    };
    arguments.setNodeChildren = [](void* pMem, void** ppChildren, unsigned childCount, void*) {
        auto* pNode = static_cast<InnerNode*>(pMem);
        for (unsigned i = 0; i < childCount; i++) {
            auto* pChild = static_cast<BVHNode*>(ppChildren[i]);
            pNode->children.push_back(pChild);
        }
    };
    arguments.setNodeBounds = [](void* pMem, const RTCBounds** ppBounds, unsigned childCount, void*) {
        auto* pNode = static_cast<InnerNode*>(pMem);
        for (unsigned i = 0; i < childCount; i++) {
            const auto* pBounds = ppBounds[i];
            pNode->childBounds.push_back(Bounds(*pBounds));
        }
    };
    arguments.createLeaf = [](RTCThreadLocalAllocator alloc, const RTCBuildPrimitive* prims, size_t numPrims, void* userPtr) -> void* {
        void* pMem = rtcThreadLocalAlloc(alloc, sizeof(LeafNode), std::alignment_of_v<LeafNode>);
        auto* pNode = new (pMem) LeafNode();

        const auto* pRoot = static_cast<const SceneNode*>(userPtr);

        pNode->numPrimitives = static_cast<unsigned>(numPrims);

        for (size_t i = 0; i < numPrims; i++) {
            const auto& prim = prims[i];
            if (prim.geomID == 0) {
                // Scene object
                pNode->sceneObjects.push_back(pRoot->objects[prim.primID].get());
            } else {
                // Scene node
                const auto& [pChild, optTransform] = pRoot->children[prim.primID];
                pNode->sceneNodes.push_back(std::pair { pChild.get(), optTransform });
            }
        }
        return static_cast<BVHNode*>(pNode);
    };
    spdlog::info("Constructing temporary BVH over scene objects/instances");
    auto* pBvhRoot = reinterpret_cast<BVHNode*>(rtcBuildBVH(&arguments));

    std::function<std::pair<size_t, std::unordered_map<const void*, size_t>>(BVHNode*)> computePrimCount =
        [&](BVHNode* pNode) -> std::pair<size_t, std::unordered_map<const void*, size_t>> {
        if (auto* pInnerNode = dynamic_cast<InnerNode*>(pNode)) {
            pInnerNode->numPrimitives = 0;

            std::unordered_map<const void*, size_t> countedEntities;
            for (auto* pChild : pInnerNode->children) {
                const auto [childPrimitives, childEntities] = computePrimCount(pChild);
                for (auto [pEntity, numPrimitives] : childEntities) {
                    // Count SceneNode(trees)/SceneObjects that appear in multiple children only once.
                    if (countedEntities.find(pEntity) == std::end(countedEntities)) {
                        countedEntities[pEntity] = numPrimitives;
                        pInnerNode->numPrimitives += numPrimitives;
                    }
                }
            }

            return { pInnerNode->numPrimitives, std::move(countedEntities) };
        } else if (auto* pLeafNode = dynamic_cast<LeafNode*>(pNode)) {
            pLeafNode->numPrimitives = 0;

            std::unordered_map<const void*, size_t> countedEntities;
            for (const SceneObject* pSceneObject : pLeafNode->sceneObjects) {
                const Shape* pShape = pSceneObject->pShape.get();
                const void* pEntity = reinterpret_cast<const void*>(pShape);

                if (countedEntities.find(pEntity) == std::end(countedEntities)) {
                    const size_t numPrims = pShape->numPrimitives();
                    countedEntities[pEntity] = numPrims;
                    pLeafNode->numPrimitives += numPrims;
                }
            }
            for (const auto& [pSceneNode, _] : pLeafNode->sceneNodes) {
                // TODO: don't count duplicate objects within a subtree
                const size_t numPrims = subTreePrimitiveCount(pSceneNode);
                const void* pEntity = reinterpret_cast<const void*>(pSceneNode);
                if (countedEntities.find(pEntity) == std::end(countedEntities)) {
                    countedEntities[pEntity] = numPrims;
                    pLeafNode->numPrimitives += numPrims;
                }
            }

            return { pLeafNode->numPrimitives, std::move(countedEntities) };
        } else {
            throw std::runtime_error("Unknown BVH node type");
        }
    };
    spdlog::info("Counting number of primitives per BVH node");
    (void)computePrimCount(pBvhRoot);

    spdlog::info("Number of unique primitives: {}", pBvhRoot->numPrimitives);

    std::function<SubScene(BVHNode*)>
        flattenSubTree = [&](BVHNode* pNode) {
            if (const auto* pInnerNode = dynamic_cast<const InnerNode*>(pNode)) {
                SubScene outScene;
                for (auto* pChild : pInnerNode->children) {
                    auto childOutScene = flattenSubTree(pChild);

                    outScene.sceneNodes.reserve(outScene.sceneNodes.size() + childOutScene.sceneNodes.size());
                    std::copy(std::begin(childOutScene.sceneNodes), std::end(childOutScene.sceneNodes), std::back_inserter(outScene.sceneNodes));

                    outScene.sceneObjects.reserve(outScene.sceneObjects.size() + childOutScene.sceneObjects.size());
                    std::copy(std::begin(childOutScene.sceneObjects), std::end(childOutScene.sceneObjects), std::back_inserter(outScene.sceneObjects));
                }
                return outScene;
            } else if (auto* pLeafNode = dynamic_cast<LeafNode*>(pNode)) {
                SubScene outScene;
                outScene.sceneNodes = std::move(pLeafNode->sceneNodes);
                outScene.sceneObjects = std::move(pLeafNode->sceneObjects);
                return outScene;
            } else {
                throw std::runtime_error("Unknown BVH node type");
            }
        };

    std::vector<SubScene> subScenes;
    std::mutex subScenesMutex;
    std::function<void(BVHNode*)> computeSubScenes = [&](BVHNode* pNode) {
        if (pNode->numPrimitives > primitivesPerSubScene) {
            tbb::task_group tg;
            if (auto* pInnerNode = dynamic_cast<InnerNode*>(pNode)) {
                for (auto* pChild : pInnerNode->children) {
                    tg.run([=]() {
                        computeSubScenes(pChild);
                    });
                }
            } else {
                std::lock_guard l { subScenesMutex };
                subScenes.push_back(flattenSubTree(pNode));
            }
            tg.wait();
        } else {
            std::lock_guard l { subScenesMutex };
            subScenes.push_back(flattenSubTree(pNode));
        }
    };
    spdlog::info("Creating sub scenes");
    computeSubScenes(pBvhRoot);

    spdlog::info("Freeing temporary BVH");
    rtcReleaseBVH(bvh);

    return subScenes;
}

std::vector<Shape*> getSubSceneShapes(const SubScene& subScene)
{
    OPTICK_EVENT();

    std::unordered_set<Shape*> visitedShapes;
    for (const auto& pSceneObject : subScene.sceneObjects) {
        visitedShapes.insert(pSceneObject->pShape.get());
    }

    // Load instanced shapes
    std::function<void(const SceneNode*)> visitRecurse = [&](const SceneNode* pSceneNode) {
        for (const auto& pSceneObject : pSceneNode->objects) {
            visitedShapes.insert(pSceneObject->pShape.get());
        }
        for (const auto& [pChild, _] : pSceneNode->children) {
            visitRecurse(pChild.get());
        }
    };
    for (const auto& [pSceneNode, _] : subScene.sceneNodes)
        visitRecurse(pSceneNode);

    std::vector<Shape*> result { visitedShapes.size() };
    std::copy(std::begin(visitedShapes), std::end(visitedShapes), std::begin(result));
    return result;
}

std::vector<tasking::CachedPtr<Shape>> makeSubSceneResident(const pandora::SubScene& subScene, tasking::LRUCacheTS& geometryCache)
{
    OPTICK_EVENT();

    std::vector<tasking::CachedPtr<Shape>> owningPtrs;
    for (const auto& pSceneObject : subScene.sceneObjects) {
        owningPtrs.emplace_back(geometryCache.makeResident(pSceneObject->pShape.get()));
    }

    std::function<void(const SceneNode*)> makeResidentRecurse = [&](const SceneNode* pSceneNode) {
        for (const auto& pSceneObject : pSceneNode->objects) {
            owningPtrs.emplace_back(geometryCache.makeResident(pSceneObject->pShape.get()));
        }

        for (const auto& [pChild, _] : pSceneNode->children) {
            makeResidentRecurse(pChild.get());
        }
    };
    for (const auto& [pChild, _] : subScene.sceneNodes) {
        makeResidentRecurse(pChild);
    }

    return owningPtrs;
}

pandora::SparseVoxelDAG createSVDAGfromSubScene(const pandora::SubScene& subScene, int resolution)
{
    OPTICK_EVENT();

    const Bounds bounds = subScene.computeBounds();

    VoxelGrid grid { bounds, resolution };
    for (const auto& sceneObject : subScene.sceneObjects) {
        Shape* pShape = sceneObject->pShape.get();
        //auto shapeOwner = m_pGeometryCache->makeResident(pShape);
        pShape->voxelize(grid);
    }

    std::function<void(const SceneNode*, glm::mat4)> voxelizeRecurse = [&](const SceneNode* pSceneNode, glm::mat4 transform) {
        for (const auto& sceneObject : pSceneNode->objects) {
            Shape* pShape = sceneObject->pShape.get();
            //auto shapeOwner = m_pGeometryCache->makeResident(pShape);
            pShape->voxelize(grid, transform);
        }

        for (const auto& [pChild, optTransform] : pSceneNode->children) {
            glm::mat4 childTransform = transform;
            if (optTransform)
                childTransform *= optTransform.value();

            voxelizeRecurse(pChild.get(), childTransform);
        }
    };

    for (const auto& [pChild, optTransform] : subScene.sceneNodes) {
        const glm::mat4 transform = optTransform ? optTransform.value() : glm::identity<glm::mat4>();
        voxelizeRecurse(pChild, transform);
    }

    // SVO is at (1, 1, 1) to (2, 2, 2)
    const float maxDim = maxComponent(bounds.extent());

    return SparseVoxelDAG { grid };
}

void splitLargeSceneObjects(pandora::SceneNode* pSceneNode, tasking::LRUCacheTS& oldCache, tasking::CacheBuilder& newCacheBuilder, RTCDevice embreeDevice, unsigned maxSize)
{
    OPTICK_EVENT();

    // Only split a shape once (even if it is instanced multiple times)
    std::unordered_map<Shape*, std::vector<std::shared_ptr<Shape>>> splitShapes;
    std::unordered_set<Shape*> cachedShapes;

    std::vector<std::shared_ptr<SceneObject>> outObjects;
    for (const auto& pSceneObject : pSceneNode->objects) { // Only split non-instanced objects
        Shape* pShape = pSceneObject->pShape.get();

        if (!pShape)
            continue;

        auto pShapeOwner = oldCache.makeResident(pShape);
        if (pShape->numPrimitives() > maxSize) {
            if (pSceneObject->pAreaLight) {
                spdlog::error("Shape attached to scene object with area light cannot be split");
                newCacheBuilder.registerCacheable(pShape, true);
                cachedShapes.insert(pShape);
                outObjects.push_back(pSceneObject);
            } else if (TriangleShape* pTriangleShape = dynamic_cast<TriangleShape*>(pShape)) {
                if (auto iter = splitShapes.find(pShape); iter != std::end(splitShapes)) {
                    for (const auto& pSubShape : iter->second) {
                        auto pSubSceneObject = std::make_shared<SceneObject>(*pSceneObject);
                        pSubSceneObject->pShape = pSubShape;
                        outObjects.push_back(pSubSceneObject);
                    }
                } else {
                    auto subShapes = splitLargeTriangleShape(*pTriangleShape, maxSize, embreeDevice);
                    oldCache.forceEvict(pShape);
                    for (const auto& pSubShape : subShapes) {
                        auto pSubSceneObject = std::make_shared<SceneObject>(*pSceneObject);
                        pSubSceneObject->pShape = pSubShape;
                        newCacheBuilder.registerCacheable(pSubShape.get(), true);
                        cachedShapes.insert(pSubShape.get());
                        outObjects.push_back(pSubSceneObject);
                    }

                    splitShapes[pShape] = subShapes;
                }
            } else {
                spdlog::error("Shape encountered with too many primitives but no way to split it");
                newCacheBuilder.registerCacheable(pShape, true);
                cachedShapes.insert(pShape);
                outObjects.push_back(pSceneObject);
            }
        } else {
            newCacheBuilder.registerCacheable(pShape, true);
            cachedShapes.insert(pShape);
            outObjects.push_back(pSceneObject);
        }
    }
    pSceneNode->objects = std::move(outObjects);

    // This part is a small nightmare so just ignore it for now.
    // Have to deal with multiple nodes pointing to the same node/subtree, nodes pointing to the same SceneObject and SceneObjects pointing to the same shape.
    // So only update the SceneObjects if the shape they point to was split, but do not split anything new we encounter.
    spdlog::info("INSTANCING");
    for (const auto& [pChild, _] : pSceneNode->children)
        replaceShapeBySplitShapesRecurse(pChild.get(), oldCache, newCacheBuilder, cachedShapes, splitShapes);

    spdlog::info("DONE");
}

static std::vector<std::shared_ptr<Shape>> splitLargeTriangleShape(const TriangleShape& shape, unsigned maxSize, RTCDevice embreeDevice)
{
    OPTICK_EVENT();

    // Split a large shape into smaller shapes with maxSize/2 to maxSize primitives.
    // The Embree BVH builder API used to efficiently partition the shapes primitives into groups.
    std::vector<RTCBuildPrimitive> embreeBuildPrimitives;
    embreeBuildPrimitives.resize(shape.numPrimitives());
    for (unsigned primID = 0; primID < shape.numPrimitives(); primID++) {
        auto bounds = shape.getPrimitiveBounds(primID);

        RTCBuildPrimitive primitive;
        primitive.lower_x = bounds.min.x;
        primitive.lower_y = bounds.min.y;
        primitive.lower_z = bounds.min.z;
        primitive.upper_x = bounds.max.x;
        primitive.upper_y = bounds.max.y;
        primitive.upper_z = bounds.max.z;
        primitive.geomID = 0;
        primitive.primID = primID;
        embreeBuildPrimitives[primID] = primitive;
    }

    // Build a coarse BVH using the Embree BVH builder API
    RTCBVH bvh = rtcNewBVH(embreeDevice);

    struct UserData {
        const TriangleShape& shape;
        tbb::concurrent_vector<std::shared_ptr<TriangleShape>> subShapes {};
    };
    UserData userData { shape };

    RTCBuildArguments arguments = rtcDefaultBuildArguments();
    arguments.byteSize = sizeof(arguments);
    arguments.buildFlags = RTC_BUILD_FLAG_NONE;
    arguments.buildQuality = RTC_BUILD_QUALITY_MEDIUM; // High build quality requires spatial splits
    arguments.maxBranchingFactor = 2;
    arguments.minLeafSize = maxSize; // Stop splitting when number of prims is below minLeafSize
    arguments.maxLeafSize = maxSize; // This is a hard constraint (always split when number of prims is larger than maxLeafSize)
    arguments.bvh = bvh;
    arguments.primitives = embreeBuildPrimitives.data();
    arguments.primitiveCount = embreeBuildPrimitives.size();
    arguments.primitiveArrayCapacity = embreeBuildPrimitives.size();
    arguments.userPtr = &userData;
    arguments.createNode = [](RTCThreadLocalAllocator, unsigned, void*) -> void* {
        return nullptr;
    };
    arguments.setNodeChildren = [](void*, void**, unsigned, void*) {};
    arguments.setNodeBounds = [](void*, const RTCBounds**, unsigned, void*) {};
    arguments.createLeaf = [](RTCThreadLocalAllocator alloc, const RTCBuildPrimitive* prims, size_t numPrims, void* userPtr) -> void* {
        UserData& userData = *reinterpret_cast<UserData*>(userPtr);

        std::vector<unsigned> primitiveIDs;
        primitiveIDs.resize(numPrims);
        for (size_t i = 0; i < numPrims; i++) {
            primitiveIDs[i] = prims[i].primID;
        }

        userData.subShapes.push_back(std::make_shared<TriangleShape>(userData.shape.subMesh(primitiveIDs)));
        return nullptr;
    };

    rtcBuildBVH(&arguments);
    rtcReleaseBVH(bvh);

    std::vector<std::shared_ptr<Shape>> subShapes;
    std::copy(std::begin(userData.subShapes), std::end(userData.subShapes), std::back_inserter(subShapes));
    return subShapes;
}

static void replaceShapeBySplitShapesRecurse(
    SceneNode* pSceneNode,
    tasking::LRUCacheTS& oldCache,
    tasking::CacheBuilder& newCacheBuilder,
    std::unordered_set<Shape*>& cachedShapes,
    const std::unordered_map<Shape*, std::vector<std::shared_ptr<Shape>>>& splitShapes)
{
    OPTICK_EVENT();

    std::vector<std::shared_ptr<SceneObject>> outObjects;
    for (const auto& pSceneObject : pSceneNode->objects) {
        Shape* pShape = pSceneObject->pShape.get();

        if (auto iter = splitShapes.find(pShape); iter != std::end(splitShapes)) {
            for (const auto& pSubShape : iter->second) {
                auto pSubSceneObject = std::make_shared<SceneObject>(*pSceneObject);
                pSubSceneObject->pShape = pSubShape;
                outObjects.push_back(pSubSceneObject);
            }
        } else if (cachedShapes.find(pShape) == std::end(cachedShapes)) {
            auto pShapeOwner = oldCache.makeResident(pShape);
            newCacheBuilder.registerCacheable(pShape, true);
            cachedShapes.insert(pShape);
            outObjects.push_back(pSceneObject);
        } else {
            outObjects.push_back(pSceneObject);
        }
    }
    pSceneNode->objects = std::move(outObjects);

    for (const auto& [pChild, _] : pSceneNode->children) {
        replaceShapeBySplitShapesRecurse(pChild.get(), oldCache, newCacheBuilder, cachedShapes, splitShapes);
    }
}

}