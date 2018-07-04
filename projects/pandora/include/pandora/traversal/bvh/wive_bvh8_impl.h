#include <EASTL/fixed_map.h>
#include <EASTL/fixed_set.h>
#include <EASTL/fixed_vector.h>
#include <algorithm>
#include <array>
#include <cassert>

namespace pandora {

template <typename LeafObj>
inline bool WiVeBVH8<LeafObj>::intersect(Ray& ray, SurfaceInteraction& si) const
{
    si.sceneObject = nullptr;
    bool hit = false;

    SIMDRay simdRay;
    simdRay.originX = simd::vec8_f32(ray.origin.x);
    simdRay.originY = simd::vec8_f32(ray.origin.y);
    simdRay.originZ = simd::vec8_f32(ray.origin.z);
    simdRay.invDirectionX = simd::vec8_f32(1.0f / ray.direction.x);
    simdRay.invDirectionY = simd::vec8_f32(1.0f / ray.direction.y);
    simdRay.invDirectionZ = simd::vec8_f32(1.0f / ray.direction.z);
    simdRay.tnear = simd::vec8_f32(ray.tnear);
    simdRay.tfar = simd::vec8_f32(ray.tfar);
    simdRay.raySignShiftAmount = simd::vec8_u32(signShiftAmount(ray.direction.x > 0, ray.direction.y > 0, ray.direction.z > 0));

    struct StackItem {
        uint32_t compressedNodeHandle;
        float distance;
    };
    eastl::fixed_vector<StackItem, 16> stack;
    stack.push_back(StackItem{ compressHandleInner(m_rootHandle), 0.0f });
    while (!stack.empty()) {
        StackItem item = stack.back();
        stack.pop_back();

        if (ray.tfar < item.distance)
            continue;

        //std::cout << "Visiting (compressed): " << std::bitset<32>(item.compressedNodeHandle) << std::endl;
        uint32_t handle = decompressNodeHandle(item.compressedNodeHandle);
        if (isInnerNode(item.compressedNodeHandle)) {
            // Inner node
            simd::vec8_u32 childrenSIMD;
            simd::vec8_f32 distancesSIMD;
            uint32_t numChildren;
            traverseCluster(&m_innerNodeAllocator->get(handle), simdRay, childrenSIMD, distancesSIMD, numChildren);

            std::array<uint32_t, 8> children;
            std::array<float, 8> distances;
            childrenSIMD.store(children);
            distancesSIMD.store(distances);
			for (uint32_t i = 0; i < numChildren; i++) {
				//stack.push_back(StackItem{ children[i], distances[i] });
                stack.push_back(StackItem{ children[numChildren - i - 1], distances[numChildren - i - 1] });
            }
        } else {
#ifndef NDEBUG
            if (isEmptyNode(item.compressedNodeHandle))
                THROW_ERROR("Empty node in traversal");
            assert(isLeafNode(item.compressedNodeHandle));
#endif
            // Leaf node
            hit |= intersectLeaf(&m_leafNodeAllocator->get(handle), leafNodePrimitiveCount(item.compressedNodeHandle), ray, si);
            simdRay.tfar.broadcast(ray.tfar);
        }
    }

    si.wo = -ray.direction;
    return hit;
}

template <typename LeafObj>
inline void WiVeBVH8<LeafObj>::traverseCluster(const BVHNode* n, const SIMDRay& ray, simd::vec8_u32& outChildren, simd::vec8_f32& outDistances, uint32_t& outNumChildren) const
{
    simd::vec8_f32 tx1 = (n->minX - ray.originX) * ray.invDirectionX;
    simd::vec8_f32 tx2 = (n->maxX - ray.originX) * ray.invDirectionX;
    simd::vec8_f32 ty1 = (n->minY - ray.originY) * ray.invDirectionY;
    simd::vec8_f32 ty2 = (n->maxY - ray.originY) * ray.invDirectionY;
    simd::vec8_f32 tz1 = (n->minZ - ray.originZ) * ray.invDirectionZ;
    simd::vec8_f32 tz2 = (n->maxZ - ray.originZ) * ray.invDirectionZ;
    simd::vec8_f32 txMin = simd::min(tx1, tx2);
    simd::vec8_f32 tyMin = simd::min(ty1, ty2);
    simd::vec8_f32 tzMin = simd::min(tz1, tz2);
    simd::vec8_f32 txMax = simd::max(tx1, tx2);
    simd::vec8_f32 tyMax = simd::max(ty1, ty2);
    simd::vec8_f32 tzMax = simd::max(tz1, tz2);
    simd::vec8_f32 tmin = simd::max(ray.tnear, simd::max(txMin, simd::max(tyMin, tzMin)));
    simd::vec8_f32 tmax = simd::min(ray.tfar, simd::min(txMax, simd::min(tyMax, tzMax)));

    const static simd::vec8_u32 indexMask(0b111);
    const static simd::vec8_u32 simd24(24);
    simd::vec8_u32 index = (n->permutationOffsets >> ray.raySignShiftAmount) & indexMask;

    tmin = tmin.permute(index);
    tmax = tmax.permute(index);
    simd::mask8 mask = tmin < tmax;
    outChildren = n->children.permute(index).compress(mask);
    outDistances = tmin.compress(mask);
    outNumChildren = mask.count();
}

template <typename LeafObj>
inline bool WiVeBVH8<LeafObj>::intersectLeaf(const BVHLeaf* n, uint32_t primitiveCount, Ray& ray, SurfaceInteraction& si) const
{
    bool hit = false;
    const auto* leafObjectIDs = n->leafObjectIDs;
    const auto* primitiveIDs = n->primitiveIDs;
    for (uint32_t i = 0; i < 4; i++) {
		if (leafObjectIDs[i] == emptyHandle)
			break;
        hit |= m_leafObjects[i]->intersectPrimitive(primitiveIDs[i], ray, si);
    }
    return hit;
}

template <typename LeafObj>
inline void WiVeBVH8<LeafObj>::testBVH() const
{
    TestBVHData results;
    testBVHRecurse(&m_innerNodeAllocator->get(m_rootHandle), 1, results);

    std::cout << std::endl;
    std::cout << " <<< BVH Build results >>> " << std::endl;
    std::cout << "===========================" << std::endl;
    std::cout << "Primitives reached: " << results.numPrimitives << std::endl;
    std::cout << "Max depth:          " << results.maxDepth << std::endl;
    std::cout << "\nChild count histogram:\n";
    for (int i = 0; i < 8; i++)
        std::cout << i << ": " << results.numChildrenHistogram[i] << std::endl;
    std::cout << std::endl;
}

template <typename LeafObj>
inline void WiVeBVH8<LeafObj>::testBVHRecurse(const BVHNode* node, int depth, TestBVHData& out) const
{
    std::array<uint32_t, 8> children;
    node->children.store(children);

    int numChildren = 0;
    for (int i = 0; i < 8; i++) {
        if (isLeafNode(children[i])) {
            out.numPrimitives += leafNodePrimitiveCount(children[i]);
        } else if (isInnerNode(children[i])) {

            testBVHRecurse(&m_innerNodeAllocator->get(decompressNodeHandle(children[i])), depth + 1, out);
        }

        if (!isEmptyNode(children[i]))
            numChildren++;
    }
    out.maxDepth = std::max(out.maxDepth, depth);
    out.numChildrenHistogram[numChildren]++;
}

template <typename LeafObj>
inline void WiVeBVH8<LeafObj>::addObject(const LeafObj* addObject)
{
    for (unsigned primitiveID = 0; primitiveID < addObject->numPrimitives(); primitiveID++) {
        auto bounds = addObject->getPrimitiveBounds(primitiveID);

        RTCBuildPrimitive primitive;
        primitive.lower_x = bounds.min.x;
        primitive.lower_y = bounds.min.y;
        primitive.lower_z = bounds.min.z;
        primitive.upper_x = bounds.max.x;
        primitive.upper_y = bounds.max.y;
        primitive.upper_z = bounds.max.z;
        primitive.primID = primitiveID;
        primitive.geomID = (unsigned)m_leafObjects.size();

        m_primitives.push_back(primitive);
        m_leafObjects.push_back(addObject); // Vector of references is a nightmare
    }
}

template <typename LeafObj>
inline uint32_t WiVeBVH8<LeafObj>::compressHandleInner(uint32_t handle)
{
    assert(handle < (1 << 29) - 1);
    return (handle & ((1 << 29) - 1)) | (0b010 << 29);
}

template <typename LeafObj>
inline uint32_t WiVeBVH8<LeafObj>::compressHandleLeaf(uint32_t handle, uint32_t primCount)
{
    assert(primCount > 0 && primCount <= 4);
    assert(handle < (1 << 29) - 1);
    return (handle & ((1 << 29) - 1)) | ((0b100 | (primCount - 1)) << 29);
}

template <typename LeafObj>
inline uint32_t WiVeBVH8<LeafObj>::compressHandleEmpty()
{
    return 0u; // 0 | (0b000 << 29)
}

#include <bitset>
#include <iostream>
template <typename LeafObj>
inline bool WiVeBVH8<LeafObj>::isLeafNode(uint32_t compressedHandle)
{
    return ((compressedHandle >> 29) & 0b100) == 0b100; // Other 2 bits are used to store the primitive count
}

template <typename LeafObj>
inline bool WiVeBVH8<LeafObj>::isInnerNode(uint32_t compressedHandle)
{
    return ((compressedHandle >> 29) & 0b111) == 0b010;
}

template <typename LeafObj>
inline bool WiVeBVH8<LeafObj>::isEmptyNode(uint32_t compressedHandle)
{
    return ((compressedHandle >> 29) & 0b111) == 0b000;
}

template <typename LeafObj>
inline uint32_t WiVeBVH8<LeafObj>::decompressNodeHandle(uint32_t compressedHandle)
{
    return compressedHandle & ((1u << 29) - 1);
}

template <typename LeafObj>
inline uint32_t WiVeBVH8<LeafObj>::leafNodePrimitiveCount(uint32_t compressedHandle)
{
    return ((compressedHandle >> 29) & 0b011) + 1;
}

template <typename LeafObj>
inline uint32_t WiVeBVH8<LeafObj>::signShiftAmount(bool positiveX, bool positiveY, bool positiveZ)
{
    return ((positiveX ? 0b001 : 0u) | (positiveY ? 0b010 : 0u) | (positiveZ ? 0b100 : 0u)) * 3;
}

}
