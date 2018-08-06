#pragma once
#include "pandora/core/pandora.h"
#include "pandora/svo/sparse_voxel_octree.h"
#include "pandora/utility/contiguous_allocator_ts.h"
#include <EASTL/fixed_vector.h>
#include <glm/glm.hpp>
#include <gsl/span>
#include <optional>
#include <tuple>
#include <vector>
#include <memory>

namespace pandora {

class SparseVoxelDAG {
public:
	SparseVoxelDAG(const VoxelGrid& grid);
	SparseVoxelDAG(SparseVoxelDAG&&) = default;
    ~SparseVoxelDAG() = default;

	friend void compressDAGs(gsl::span<SparseVoxelDAG> svos);

    //void intersectSIMD(ispc::RaySOA rays, ispc::HitSOA hits, int N) const;
    std::optional<float> intersectScalar(Ray ray) const;

    std::pair<std::vector<glm::vec3>, std::vector<glm::ivec3>> generateSurfaceMesh() const;

private:
    using NodeOffset = uint32_t; // Either uint32_t or uint16_t

    // NOTE: child pointers are stored directly after the descriptor
    struct Descriptor {
        uint8_t leafMask;
        uint8_t validMask;

        inline bool isEmpty() const { return validMask == 0x0; }; // Completely empty node
        inline bool isFilledLeaf() const { return (validMask & leafMask) == 0xFF; }; // Completely filled leaf node
        inline bool isValid(int i) const { return validMask & (1 << i); };
        inline bool isLeaf(int i) const { return (validMask & leafMask) & (1 << i); };
		inline bool isInnerNode(int i) const { return (validMask & (~leafMask)) & (1 << i); };
        inline int numInnerNodeChildren() const { return _mm_popcnt_u32(validMask & (~leafMask)); };

        Descriptor() = default;
        //inline explicit Descriptor(const SVOChildDescriptor& v) { leafMask = v.leafMask; validMask = v.validMask; __padding = 0; };
        inline explicit Descriptor(NodeOffset v) { memcpy(this, &v, sizeof(Descriptor)); };
        inline explicit operator NodeOffset() const { return static_cast<NodeOffset>(*reinterpret_cast<const uint16_t*>(this)); };
    };
    static_assert(sizeof(Descriptor) == sizeof(uint16_t));

    NodeOffset constructSVOBreadthFirst(const VoxelGrid& grid);
    NodeOffset constructSVO(const VoxelGrid& grid);

    // CAREFULL: don't use this function during DAG construction (while m_allocator is touched)!
    const Descriptor* getChild(const Descriptor* descriptor, int idx) const;

    // SVO construction
    struct SVOConstructionQueueItem {
        Descriptor descriptor;
        eastl::fixed_vector<NodeOffset, 8> childDescriptorOffsets;
    };
    static Descriptor createStagingDescriptor(gsl::span<bool, 8> validMask, gsl::span<bool, 8> leafMask);
    static Descriptor makeInnerNode(gsl::span<SVOConstructionQueueItem, 8> children);
    eastl::fixed_vector<NodeOffset, 8> storeDescriptors(gsl::span<SVOConstructionQueueItem> children);

private:
    int m_resolution;
    
	NodeOffset m_rootNodeOffset;
	std::vector<std::pair<size_t, size_t>> m_treeLevels;
    std::vector<NodeOffset> m_allocator;
	const NodeOffset* m_data;
};

}
