#include "pandora/svo/sparse_voxel_dag.h"
#include "pandora/core/ray.h"
#include "pandora/svo/voxel_grid.h"
#include "pandora/utility/error_handling.h"
#include "pandora/utility/math.h"
#include <EASTL/fixed_vector.h>
#include <bitset>
#include <boost/functional/hash.hpp>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <glm/gtx/bit.hpp>
#include <immintrin.h>
#include <limits>
#include <morton.h>
#include <simd/simd4.h>

namespace pandora {

// http://graphics.cs.kuleuven.be/publications/BLD13OCCSVO/BLD13OCCSVO_paper.pdf
SparseVoxelDAG::SparseVoxelDAG(const VoxelGrid& grid)
    : m_resolution(grid.resolution())
{
    //m_rootNode = constructSVO(grid);
    m_rootNodeOffset = constructSVOBreadthFirst(grid);
    m_nodeAllocator.shrink_to_fit();
    m_data = m_nodeAllocator.data();
    m_leafData = m_leafAllocator.data();

    //std::cout << "Size of SparseVoxelDAG before compression: " << this->size() << " bytes" << std::endl;
}

SparseVoxelDAG::NodeOffset SparseVoxelDAG::constructSVOBreadthFirst(const VoxelGrid& grid)
{
    ALWAYS_ASSERT(isPowerOf2(m_resolution), "Resolution must be a power of 2"); // Resolution = power of 2
    int depth = intLog2(m_resolution) - 2;

    struct NodeInfoN1 {
        uint_fast32_t mortonCode; // Morton code (in level N-1)
        bool isLeaf; // Is a leaf node and descriptorOffset points into the leaf allocator array
        bool isFullyFilledLeaf; // Special case: fully filled leaf(needs to be propegated up the tree)
        NodeOffset descriptorOffset;
    };
    std::vector<NodeInfoN1> previousLevelNodes;
    std::vector<NodeInfoN1> currentLevelNodes;

    // Creates and inserts leaf nodes
    assert(m_resolution % 4 == 0);
    uint_fast32_t finalMortonCode = static_cast<uint_fast32_t>(m_resolution * m_resolution * m_resolution);
    for (uint_fast32_t mortonCode = 0; mortonCode < finalMortonCode; mortonCode += 64) {
        // Create leaf node from 4x4x4 voxel block
        std::bitset<64> leaf;
        for (uint32_t i = 0; i < 64; i++) {
            leaf[i] = grid.getMorton(mortonCode + i);
        }

        if (leaf.all()) {
            currentLevelNodes.push_back({ mortonCode >> 6, true, true, 0 });
        } else if (leaf.any()) {
            currentLevelNodes.push_back({ mortonCode >> 6, true, false, static_cast<NodeOffset>(m_leafAllocator.size()) });
            m_leafAllocator.push_back(leaf.to_ullong());
        }
    }

    auto createAndStoreDescriptor = [&](uint8_t validMask, uint8_t leafMask, const gsl::span<NodeOffset> childrenOffsets) -> NodeOffset {
        Descriptor d;
        d.validMask = validMask;
        d.leafMask = leafMask;
        assert(d.numChildren() == childrenOffsets.size());

        NodeOffset descriptorOffset = static_cast<NodeOffset>(m_nodeAllocator.size());
        m_nodeAllocator.push_back(static_cast<NodeOffset>(d));

        // Store child offsets directly after the descriptor itself
        m_nodeAllocator.insert(std::end(m_nodeAllocator), std::begin(childrenOffsets), std::end(childrenOffsets));

        return descriptorOffset;
    };

    // Work in a separate vector so m_nodeAllocator data doesnt change while inserting new descriptors.
    NodeOffset rootNodeOffset = 0;
    for (int N = 0; N < depth; N++) {
        std::swap(previousLevelNodes, currentLevelNodes);
        currentLevelNodes.clear();

        int numFullLeafs = 0;
        uint8_t validMask = 0x00;
        uint8_t leafMask = 0x00;
        eastl::fixed_vector<NodeOffset, 8> childrenOffsets;

        uint_fast32_t prevMortonCode = previousLevelNodes[0].mortonCode >> 3;
        // Loop over all the cubes of the previous (more refined level)
        for (const auto& childNodeInfo : previousLevelNodes) {
            auto mortonCodeN1 = childNodeInfo.mortonCode;
            auto mortonCodeN = mortonCodeN1 >> 3; // Morton code of the node in the current level (stripping last 3 bits)
            if (prevMortonCode != mortonCodeN) {
                if (numFullLeafs == 8) {
                    // Special case: all children are completely filled (1's): propegate this up the tree
                    currentLevelNodes.push_back({ prevMortonCode, true, true, 0 });
                } else {
                    // Different morton code (at the current level): we are finished with the previous node => store it
                    auto descriptorOffset = createAndStoreDescriptor(validMask, leafMask, childrenOffsets);
                    currentLevelNodes.push_back({ prevMortonCode, false, false, descriptorOffset });
                }
                validMask = 0;
                leafMask = 0;
                numFullLeafs = 0;
                childrenOffsets.clear();
                prevMortonCode = mortonCodeN;
            }

            auto idx = mortonCodeN1 & ((1 << 3) - 1); // Right most 3 bits
            assert((validMask & (1 << idx)) == 0); // We should never visit the same child twice
            validMask |= 1 << idx;
            if (childNodeInfo.isLeaf) {
                leafMask |= 1 << idx;
            }
            if (childNodeInfo.isFullyFilledLeaf) {
                numFullLeafs++;
            }
            childrenOffsets.push_back(childNodeInfo.descriptorOffset);
        }

        // Store final descriptor
        if (numFullLeafs == 8 && N == depth - 1) {
            // Special case when area is fully filled. Make an exception for the root node.
            currentLevelNodes.push_back({ prevMortonCode, true, true, 0 });
        } else {
            auto descriptorOffset = createAndStoreDescriptor(validMask, leafMask, childrenOffsets);
            auto lastNodeMortonCode = (previousLevelNodes.back().mortonCode >> 3);
            assert(lastNodeMortonCode == prevMortonCode);
            currentLevelNodes.push_back({ prevMortonCode, false, false, descriptorOffset });
            rootNodeOffset = descriptorOffset; // Keep track of the offset to the root node
        }
    }

    assert(currentLevelNodes.size() == 1);
    return rootNodeOffset;
}

void SparseVoxelDAG::compressDAGs(gsl::span<SparseVoxelDAG*> svos)
{
    using Descriptor = SparseVoxelDAG::Descriptor;
    using NodeOffset = SparseVoxelDAG::NodeOffset;

    struct FullDescriptor {
        Descriptor descriptor;
        eastl::fixed_vector<NodeOffset, 8> children; // Pair of [isLeaf, absoluteOffset]

        bool operator==(const FullDescriptor& other) const
        {
            // Dont need to test whether the child offset points to a leaf because this information is already included in the leafMask
            return descriptor == other.descriptor && children.size() == other.children.size() && memcmp(children.data(), other.children.data(), children.size() * sizeof(decltype(children)::value_type)) == 0;
        }
    };

    struct FullDescriptorHasher {
        std::size_t operator()(const FullDescriptor& desc) const noexcept
        {
            // https://www.boost.org/doc/libs/1_67_0/doc/html/hash/combine.html
            size_t seed = 0;
            boost::hash_combine(seed, boost::hash<uint8_t> {}(desc.descriptor.leafMask));
            boost::hash_combine(seed, boost::hash<uint8_t> {}(desc.descriptor.validMask));
            for (auto offset : desc.children) {
                //boost::hash_combine(seed, boost::hash<decltype(isLeaf)> {}(isLeaf));
                boost::hash_combine(seed, boost::hash<decltype(offset)> {}(offset));
            }
            return seed;
        }
    };

    // Store the descriptor in the final format
    std::vector<uint64_t> leafAllocator;
    std::vector<NodeOffset> nodeAllocator;
    auto storeDescriptor = [&](const FullDescriptor& fullDescriptor) -> NodeOffset {
        ALWAYS_ASSERT(nodeAllocator.size() + 1 + fullDescriptor.children.size() < std::numeric_limits<NodeOffset>::max());

        auto nodeOffset = static_cast<NodeOffset>(nodeAllocator.size());
        nodeAllocator.push_back(static_cast<NodeOffset>(fullDescriptor.descriptor));
        for (auto childOffset : fullDescriptor.children) {
            nodeAllocator.push_back(childOffset);
        }

        return nodeOffset;
    };

    auto storeLeaf = [&](const uint64_t leaf) -> NodeOffset {
        NodeOffset leafOffset = static_cast<NodeOffset>(leafAllocator.size());
        leafAllocator.push_back(leaf);
        return leafOffset;
    };

    std::unordered_map<FullDescriptor, NodeOffset, FullDescriptorHasher> descriptorLUT; // Look-up table from descriptors (pointing into DAG allocator array) to nodes in the DAG allocator array
    std::unordered_map<uint64_t, NodeOffset> leafLUT; // Look-up table from 64-bit leaf to offset in the DAG leaf allocator array

    for (auto* svo : svos) {
        std::function<NodeOffset(const Descriptor*)> recurseSVO = [&](const Descriptor* descriptor) -> NodeOffset {
            const NodeOffset* childOffsetPtr = reinterpret_cast<const NodeOffset*>(descriptor) + 1;

            FullDescriptor fullDescriptor { *descriptor, {} };
            for (int childIdx = 0; childIdx < 8; childIdx++) {
                if (descriptor->isInnerNode(childIdx)) {
                    // Child node offset into node (m_data) array
                    NodeOffset originalChildOffset = *childOffsetPtr++;
                    auto svdagChildOffset = recurseSVO(reinterpret_cast<const Descriptor*>(svo->m_data + originalChildOffset));
                    fullDescriptor.children.push_back(svdagChildOffset);
                } else if (descriptor->isLeaf(childIdx)) {
                    // Leaf offset into m_leafAllocator (m_leafData) array
                    NodeOffset originalLeafOffset = *childOffsetPtr++;
                    auto leaf = svo->m_leafData[originalLeafOffset];
                    if (auto lutIter = leafLUT.find(leaf); lutIter != leafLUT.end()) {
                        fullDescriptor.children.push_back(lutIter->second);
                    } else {
                        auto svdagLeafOffset = storeLeaf(leaf);
                        leafLUT[leaf] = svdagLeafOffset;
                        fullDescriptor.children.push_back(svdagLeafOffset);
                    }
                }
            }

            if (auto lutIter = descriptorLUT.find(fullDescriptor); lutIter != descriptorLUT.end()) {
                return lutIter->second;
            } else {
                auto svdagDescriptorOffset = storeDescriptor(fullDescriptor);
                descriptorLUT[fullDescriptor] = svdagDescriptorOffset;
                return svdagDescriptorOffset;
            }
        };

        svo->m_rootNodeOffset = recurseSVO(reinterpret_cast<const Descriptor*>(svo->m_data + svo->m_rootNodeOffset));
        svo->m_nodeAllocator.clear();
        svo->m_nodeAllocator.shrink_to_fit();
        svo->m_leafAllocator.clear();
        svo->m_nodeAllocator.shrink_to_fit();
    }

    for (auto& svo : svos) {
        svo->m_data = nodeAllocator.data(); // Set this after all work on the allocator is done (and the pointer cant change because of reallocations)
        svo->m_leafData = leafAllocator.data(); // Set this after all work on the allocator is done (and the pointer cant change because of reallocations)
    }

    // Make the first SVO owner of the data
    svos[0]->m_nodeAllocator = std::move(nodeAllocator);
    svos[0]->m_leafAllocator = std::move(leafAllocator);

    std::cout << "Combined SVDAG size after compression: " << svos[0]->sizeBytes() << " bytes" << std::endl;
}

SparseVoxelDAG::Descriptor SparseVoxelDAG::createStagingDescriptor(gsl::span<bool, 8> validMask, gsl::span<bool, 8> leafMask)
{
    // Create bit masks
    uint8_t leafMaskBits = 0x0;
    for (int i = 0; i < 8; i++)
        if (leafMask[i])
            leafMaskBits |= (1 << i);

    uint8_t validMaskBits = 0x0;
    for (int i = 0; i < 8; i++)
        if (validMask[i])
            validMaskBits |= (1 << i);

    // Create temporary descriptor
    Descriptor descriptor;
    descriptor.validMask = validMaskBits;
    descriptor.leafMask = leafMaskBits;
    return descriptor;
}

#ifdef PANDORA_ISPC_SUPPORT
void SparseVoxelDAG::intersectSIMD(ispc::RaySOA rays, ispc::HitSOA hits, int N) const
{
    THROW_ERROR("WARNING: ISPC SVDAG traversal needs to be updated to support absolute instead of relative child node offsets!");
    /*static_assert(sizeof(Descriptor) == sizeof(uint16_t));
    static_assert(std::is_same_v<RelativeNodeOffset, uint16_t> || std::is_same_v<RelativeNodeOffset, uint32_t>);

    if constexpr (std::is_same_v<RelativeNodeOffset, uint16_t>) {
        ispc::SparseVoxelDAG16 svdag;
        svdag.descriptors = reinterpret_cast<const uint16_t*>(m_data); // Using contexpr if-statements dont fix this???
        svdag.rootNodeOffset = static_cast<uint32_t>(m_rootNodeOffset);
        svdag.leafs = reinterpret_cast<const uint32_t*>(m_leafData);
        ispc::SparseVoxelDAG16_intersect(svdag, rays, hits, N);
    }
    if constexpr (std::is_same_v<RelativeNodeOffset, uint32_t>) {
        ispc::SparseVoxelDAG32 svdag;
        svdag.descriptors = reinterpret_cast<const uint32_t*>(m_data); // Using contexpr if-statements dont fix this???
        svdag.rootNodeOffset = static_cast<uint32_t>(m_rootNodeOffset);
        svdag.leafs = reinterpret_cast<const uint32_t*>(m_leafData);
        ispc::SparseVoxelDAG32_intersect(svdag, rays, hits, N);
    }*/
}
#endif

const SparseVoxelDAG::Descriptor* SparseVoxelDAG::getChild(const Descriptor* descriptorPtr, int idx) const
{
    uint32_t childMask = descriptorPtr->validMask & ((1 << idx) - 1);
    uint32_t activeChildIndex = _mm_popcnt_u64(childMask);

    const NodeOffset* firstChildPtr = reinterpret_cast<const NodeOffset*>(descriptorPtr) + 1;
    auto childOffset = *(firstChildPtr + activeChildIndex);
    return reinterpret_cast<const Descriptor*>(m_data + childOffset);
}

uint64_t SparseVoxelDAG::getLeaf(const Descriptor* descriptorPtr, int idx) const
{
    uint32_t childMask = descriptorPtr->validMask & ((1 << idx) - 1);
    uint32_t activeChildIndex = _mm_popcnt_u64(childMask);

    const NodeOffset* firstChildPtr = reinterpret_cast<const NodeOffset*>(descriptorPtr) + 1;
    NodeOffset childOffset = *(firstChildPtr + activeChildIndex);
    return m_leafData[childOffset];
}

static constexpr int CAST_STACK_DEPTH = 23; //intLog2(m_resolution);
static constexpr std::array<float, CAST_STACK_DEPTH> computeScaleExp2LUT()
{
    // scaleExp2 = exp2f(scale - CAST_STACK_DEPTH)
    std::array<float, CAST_STACK_DEPTH> ret = {};
    float scaleExp2 = 0.5f;
    for (int scale = CAST_STACK_DEPTH - 1; scale >= 0; scale--) {
        ret[scale] = scaleExp2;
        scaleExp2 *= 0.5f;
    }
    return ret;
}

// Get the lowest value from the first 3 channels of the vector
static inline float horizontalMin3(const simd::vec4_f32& v)
{
    /*simd::mask4 mask(false, false, false, true);
    simd::vec4_f32 altValues(std::numeric_limits<float>::max());
    return simd::blend(v, altValues, mask).horizontalMin();*/

    // Scalar min is faster than computing a 4 wide min + blending (masking)
    alignas(16) float values[4];
    v.storeAligned(values);
    return std::min(values[0], std::min(values[1], values[2]));
}

// Get the highest value from the first 3 channels of the vector
static inline float horizontalMax3(const simd::vec4_f32& v)
{
    /*simd::mask4 mask(false, false, false, true);
    simd::vec4_f32 altValues(std::numeric_limits<float>::lowest());
    return simd::blend(v, altValues, mask).horizontalMax();*/

    // Scalar max is faster than computing a 4 wide max + blending (masking)
    alignas(16) float values[4];
    v.storeAligned(values);
    return std::max(values[0], std::max(values[1], values[2]));
}

std::optional<float> SparseVoxelDAG::intersectScalar(Ray ray) const
{
    // Based on the reference implementation of Efficient Sparse Voxel Octrees:
    // https://github.com/poelzi/efficient-sparse-voxel-octrees/blob/master/src/octree/cuda/Raycast.inl

    // Get rid of small ray direction components to avoid division by zero
    constexpr float epsilon = 1.1920928955078125e-07f; // std::exp2f(-CAST_STACK_DEPTH);
    if (std::abs(ray.direction.x) < epsilon)
        ray.direction.x = std::copysign(epsilon, ray.direction.x);
    if (std::abs(ray.direction.y) < epsilon)
        ray.direction.y = std::copysign(epsilon, ray.direction.y);
    if (std::abs(ray.direction.z) < epsilon)
        ray.direction.z = std::copysign(epsilon, ray.direction.z);
    simd::vec4_f32 rayOrigin(ray.origin.x, ray.origin.y, ray.origin.z, 1.0f);
    simd::vec4_f32 rayDirection(ray.direction.x, ray.direction.y, ray.direction.z, 1.0f);

    // Precompute the coefficients of tx(x), ty(y) and tz(z).
    // The octree is assumed to reside at coordinates [1, 2].
    //glm::vec3 tCoef = 1.0f / -glm::abs(ray.direction);
    simd::vec4_f32 tCoef = simd::vec4_f32(1.0f) / -rayDirection.abs();
    simd::vec4_f32 tBias = tCoef * rayOrigin;

    /*// Select octant mask to mirror the coordinate system so that ray direction is negative along each axis
    uint32_t octantMask = 7;
    if (ray.direction.x > 0.0f) {
        octantMask ^= (1 << 0);
        tBias.x = 3.0f * tCoef.x - tBias.x;
    }
    if (ray.direction.y > 0.0f) {
        octantMask ^= (1 << 1);
        tBias.y = 3.0f * tCoef.y - tBias.y;
    }
    if (ray.direction.z > 0.0f) {
        octantMask ^= (1 << 2);
        tBias.z = 3.0f * tCoef.z - tBias.z;
    }*/
    simd::mask4 octantMask = rayDirection > simd::vec4_f32(0.0f);
    int octantMaskBits = (7 ^ octantMask.bitMask()) & 7; // Mask out channel 4
    tBias = simd::blend(tBias, simd::vec4_f32(3.0f) * tCoef - tBias, octantMask);

    // Initialize the current voxel to the first child of the root
    const Descriptor* parent = reinterpret_cast<const Descriptor*>(m_data + m_rootNodeOffset);
    //glm::vec3 pos = glm::vec3(1.0f);
    simd::vec4_f32 pos = simd::vec4_f32(1.0f);
    int scale = CAST_STACK_DEPTH - 1;
    constexpr std::array<float, CAST_STACK_DEPTH> scaleExp2LUT = computeScaleExp2LUT();

    // Initialize the active span of t-values
    /*float tMin = maxComponent(2.0f * tCoef - tBias);
    float tMax = minComponent(tCoef - tBias);*/
    const float tMin = std::max(0.0f, horizontalMax3(simd::vec4_f32(2.0f) * tCoef - tBias));
    const float tMax = horizontalMin3(tCoef - tBias);

    if (tMin >= tMax)
        return {};

    // Store the ray distance as a simd::vector until we have to return. This saves us from needlessly having
    // to convert between vector and scalar code (which makes this slightly faster)
    simd::vec4_f32 tMinVec(tMin);

    /*// Intersection of ray (negative in all directions) with the root node (cube at [1, 2])
    int idx = 0;
    if (1.5f * tCoef.x - tBias.x > tMin) {
        idx ^= (1 << 0);
        pos.x = 1.5f;
    }
    if (1.5f * tCoef.y - tBias.y > tMin) {
        idx ^= (1 << 1);
        pos.y = 1.5f;
    }
    if (1.5f * tCoef.z - tBias.z > tMin) {
        idx ^= (1 << 2);
        pos.z = 1.5f;
    }*/
    simd::mask4 idxMask = (simd::vec4_f32(1.5f) * tCoef - tBias) > tMin;
    int idx = idxMask.bitMask();
    pos = simd::blend(pos, simd::vec4_f32(1.5f), idxMask);

    // Traverse voxels along the ray as long as the current voxel stays within the octree
    std::array<const Descriptor*, CAST_STACK_DEPTH + 1> stack;

    int depthInLeaf = 0; // 1 if at 2x2x2 level, 2 if at 4x4x4 level, 0 otherwise
    uint64_t leafData;
    while (scale < CAST_STACK_DEPTH) {
        // === INTERSECT ===
        // Determine the maximum t-value of the cube by evaluating tx(), ty() and tz() at its corner
        //glm::vec3 tCorner = pos * tCoef - tBias;
        //float tcMax = minComponent(tCorner);
        simd::vec4_f32 tCorner = pos * tCoef - tBias;
        simd::vec4_f32 tcMax = simd::intBitsToFloat(simd::floatBitsToInt(tCorner) | simd::vec4_u32(0x0, 0x0, 0x0, 0x7FFFFFF)).horizontalMinVec();// Slightly faster
        //simd::vec4_f32 tcMax = simd::blend(tCorner, simd::vec4_f32(std::numeric_limits<float>::max()), simd::mask4(false,false,false,true)).horizontalMinVec();

        int bitIndex = 63 - ((idx & 0b111111) ^ (octantMaskBits | (octantMaskBits << 3)));
        if (depthInLeaf == 2 && (leafData & (1llu << bitIndex))) {
            break;
        }

        // Process voxel if the corresponding bit in the parents valid mask is set
        int childIndex = 7 - ((idx & 0b111) ^ octantMaskBits);
        if (depthInLeaf == 1 || (depthInLeaf == 0 && parent->isValid(childIndex))) {
            float half = scaleExp2LUT[scale - 1];
            simd::vec4_f32 tCenter = simd::vec4_f32(half) * tCoef + tCorner;

            // === PUSH ===
            stack[scale] = parent;

            if (depthInLeaf == 0) {
                if (parent->isLeaf(childIndex)) {
                    leafData = getLeaf(parent, childIndex);
                    depthInLeaf = 1;
                    parent = nullptr;
                } else {
                    // Find child descriptor corresponding to the current voxel
                    parent = getChild(parent, childIndex);
                }
            } else {
                depthInLeaf++; // 1 ==> 2
            }

            // Select the child voxel that the ray enters first.
            idx = (idx & 0b111) << 3; // Keep the index in the current level which is required for accessing leaf node bits (which are 2 levels deep conceptually)
            scale--;
            //scaleExp2 = half;
            /*if (tCenter.x > tMin) {
                idx ^= (1 << 0);
                pos.x += half; // scaleExp2;
            }
            if (tCenter.y > tMin) {
                idx ^= (1 << 1);
                pos.y += half; // scaleExp2;
            }
            if (tCenter.z > tMin) {
                idx ^= (1 << 2);
                pos.z += half; // scaleExp2;
            }*/
            {
                auto mask = tCenter > tMinVec;
                idx ^= (mask.bitMask() & 7); // Mask out 4th channel
                pos = simd::blend(pos, pos + half, mask);
            }
        } else {
            // === ADVANCE ===
            const float scaleExp2 = scaleExp2LUT[scale];

            /*// Step along the ray
            int stepMask = 0;
            if (tCorner.x <= tcMax) {
                stepMask ^= (1 << 0);
                pos.x -= scaleExp2;
            }
            if (tCorner.y <= tcMax) {
                stepMask ^= (1 << 1);
                pos.y -= scaleExp2;
            }
            if (tCorner.z <= tcMax) {
                stepMask ^= (1 << 2);
                pos.z -= scaleExp2;
            }*/
            simd::mask4 stepMask = tCorner <= tcMax;
            int stepMaskBits = stepMask.bitMask() & 7;
            pos = simd::blend(pos, pos - scaleExp2, stepMask);

            // Update active t-span and flip bits of the child slot index
            tMinVec = tcMax;
            idx ^= stepMaskBits;

            // Proceed with pop if the bit flip disagree with the ray direction
            if ((idx & stepMaskBits) != 0) {
                // === POP ===
                // Find the highest differing bit between the two positions
                unsigned differingBits = 0;
                /*if ((stepMask & (1 << 0)) != 0) {
                    differingBits |= floatAsInt(pos.x) ^ floatAsInt(pos.x + scaleExp2);
                }
                if ((stepMask & (1 << 1)) != 0) {
                    differingBits |= floatAsInt(pos.y) ^ floatAsInt(pos.y + scaleExp2);
                }
                if ((stepMask & (1 << 2)) != 0) {
                    differingBits |= floatAsInt(pos.z) ^ floatAsInt(pos.z + scaleExp2);
                }*/
                simd::vec4_u32 differingBitsVec = simd::blend(simd::vec4_u32(0u), simd::floatBitsToInt(pos) ^ simd::floatBitsToInt(pos + simd::vec4_f32(scaleExp2)), stepMask);
                {
                    alignas(16) uint32_t differingBitsArray[4];
                    differingBitsVec.storeAligned(differingBitsArray);
                    differingBits = differingBitsArray[0] | differingBitsArray[1] | differingBitsArray[2];
                }

                int oldScale = scale;
                scale = (floatAsInt((float)differingBits) >> 23) - 127; // Position of the highest set bit (equivalent of a reverse bit scan)
                //scaleExp2 = intAsFloat((scale - CAST_STACK_DEPTH + 127) << 23); // exp2f(scale - s_max)
                assert(oldScale < scale);
                depthInLeaf = std::max(0, depthInLeaf - (scale - oldScale));

                // Restore parent voxel from the stack
                parent = stack[scale];
            }

            // Round cube position and extract child slot index
            simd::vec4_u32 sh = simd::floatBitsToInt(pos) >> scale;
            pos = simd::intBitsToFloat(sh << scale);
            const simd::vec4_u32 shMask1 = sh & simd::vec4_u32(1);
            const simd::vec4_u32 shMask2 = sh & simd::vec4_u32(2);
            const simd::vec4_u32 shMask1Shifted = shMask1 << simd::vec4_u32(0, 1, 2, 0);
            const simd::vec4_u32 shMask2Shifted = shMask2 << simd::vec4_u32(2, 3, 4, 0);
            const simd::vec4_u32 partialIdx = shMask1Shifted | shMask2Shifted;
            alignas(16) uint32_t partialIdxArray[4];
            partialIdx.storeAligned(partialIdxArray);
            idx = partialIdxArray[0] | partialIdxArray[1] | partialIdxArray[2];

            /*
            // Round cube position and extract child slot index
            int shx = floatAsInt(pos.x) >> scale;
            int shy = floatAsInt(pos.y) >> scale;
            int shz = floatAsInt(pos.z) >> scale;
            pos.x = intAsFloat(shx << scale);
            pos.y = intAsFloat(shy << scale);
            pos.z = intAsFloat(shz << scale);
            //idx = ((shx & 1) << 0) | ((shy & 1) << 1) | ((shz & 1) << 2) | (((shx & 2) >> 1) << 3) | (((shy & 2) >> 1) << 4) | (((shz & 2) >> 1) << 5);
            idx= (shx & 1) | ((shy & 1) << 1) | ((shz & 1) << 2) | ((shx & 2) << 2) | ((shy & 2) << 3) | ((shz & 2) << 4);
            */
        } // Push / pop
    } // While

    // Indicate miss if we are outside the octree
    if (scale >= CAST_STACK_DEPTH) {
        return {};
    } else {
        // Output result
        alignas(16) float ret[4];
        tMinVec.storeAligned(ret);
        return ret[0];
    }
}

std::pair<std::vector<glm::vec3>, std::vector<glm::ivec3>> SparseVoxelDAG::generateSurfaceMesh() const
{
    std::vector<glm::vec3> positions;
    std::vector<glm::ivec3> triangles;

    struct StackItem {
        const Descriptor* descriptor;
        glm::uvec3 start;
        unsigned extent;
    };
    std::vector<StackItem> stack = { { reinterpret_cast<const Descriptor*>(m_data + m_rootNodeOffset), glm::uvec3(0), m_resolution } };
    while (!stack.empty()) {
        auto stackItem = stack.back();
        stack.pop_back();

        // Loop visit in morton order?
        unsigned halfExtent = stackItem.extent / 2;

        for (uint_fast32_t childIdx = 0; childIdx < 8; childIdx++) {
            uint_fast16_t x, y, z;
            libmorton::morton3D_32_decode(childIdx, x, y, z);
            glm::uvec3 cubeStart = stackItem.start + glm::uvec3(x * halfExtent, y * halfExtent, z * halfExtent);

            if (stackItem.descriptor->isValid(childIdx)) {
                if (!stackItem.descriptor->isLeaf(childIdx)) {
                    //uint32_t childOffset = *(reinterpret_cast<const uint32_t*>(stackItem.descriptor) + childID++);
                    //const auto* childDescriptor = reinterpret_cast<const Descriptor*>(&m_nodeAllocator[childOffset]);
                    const auto* childDescriptor = getChild(stackItem.descriptor, childIdx);
                    stack.push_back(StackItem { childDescriptor, cubeStart, halfExtent });
                } else {
                    // https://github.com/ddiakopoulos/tinyply/blob/master/source/example.cpp
                    const glm::vec3 cubePositions[] = {
                        { 0, 0, 0 }, { 0, 0, +1 }, { 0, +1, +1 }, { 0, +1, 0 },
                        { +1, 0, +1 }, { +1, 0, 0 }, { +1, +1, 0 }, { +1, +1, +1 },
                        { 0, 0, 0 }, { +1, 0, 0 }, { +1, 0, +1 }, { 0, 0, +1 },
                        { +1, +1, 0 }, { 0, +1, 0 }, { 0, +1, +1 }, { +1, +1, +1 },
                        { 0, 0, 0 }, { 0, +1, 0 }, { +1, +1, 0 }, { +1, 0, 0 },
                        { 0, +1, +1 }, { 0, 0, +1 }, { +1, 0, +1 }, { +1, +1, +1 }
                    };
                    std::array quads = { glm::ivec4 { 0, 1, 2, 3 }, glm::ivec4 { 4, 5, 6, 7 }, glm::ivec4 { 8, 9, 10, 11 }, glm::ivec4 { 12, 13, 14, 15 }, glm::ivec4 { 16, 17, 18, 19 }, glm::ivec4 { 20, 21, 22, 23 } };

                    unsigned voxelExtent = halfExtent / 4;
                    uint64_t leafNode = getLeaf(stackItem.descriptor, childIdx);
                    for (int v = 0; v < 64; v++) {
                        if (leafNode & (1llu << v)) {
                            glm::ivec3 offset((int)positions.size());
                            for (auto& q : quads) {
                                triangles.push_back(glm::ivec3 { q.x, q.y, q.z } + offset);
                                triangles.push_back(glm::ivec3 { q.x, q.z, q.w } + offset);
                            }

                            uint_fast16_t vX, vY, vZ;
                            libmorton::morton3D_32_decode(v, vX, vY, vZ);
                            glm::uvec3 voxelPos(vX, vY, vZ);
                            for (int t = 0; t < 24; ++t) {
                                positions.push_back(glm::vec3(cubeStart + voxelPos * voxelExtent) + static_cast<float>(voxelExtent) * cubePositions[t]);
                            }
                        }
                    }
                }
            }
        }
    }
    return { std::move(positions), std::move(triangles) };
}

size_t SparseVoxelDAG::sizeBytes() const
{
    size_t size = sizeof(decltype(*this));
    size += m_nodeAllocator.size() * sizeof(decltype(m_nodeAllocator)::value_type);
    size += m_leafAllocator.size() * sizeof(decltype(m_leafAllocator)::value_type);
    return size;
}

}
