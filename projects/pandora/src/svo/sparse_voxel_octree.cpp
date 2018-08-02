#include "pandora/svo/sparse_voxel_octree.h"
#include "pandora/core/ray.h"
#include "pandora/svo/voxel_grid.h"
#include "pandora/utility/error_handling.h"
#include "pandora/utility/math.h"
#include <EASTL/fixed_vector.h>
#include <array>
#include <cmath>
#include <cstring>
#include <libmorton/morton.h>

namespace pandora {

// http://graphics.cs.kuleuven.be/publications/BLD13OCCSVO/BLD13OCCSVO_paper.pdf
SparseVoxelOctree::SparseVoxelOctree(const VoxelGrid& grid)
    : m_resolution(grid.resolution())
{
    uint_fast32_t resolution = static_cast<uint_fast32_t>(grid.resolution());
    int depth = intLog2(resolution) - 1;
    uint_fast32_t finalMortonCode = resolution * resolution * resolution;

    uint_fast32_t inputMortonCode = 0;
    auto consume = [&]() {
        // Create leaf node from 2x2x2 voxel block
        std::array<bool, 8> leafMask;
        std::array<bool, 8> validMask;
        for (int i = 0; i < 8; i++) {
            bool v = grid.getMorton(inputMortonCode++);
            validMask[i] = v;
            leafMask[i] = v;
        }
        return createStagingDescriptor(validMask, leafMask);
    };
    auto hasInput = [&]() {
        return inputMortonCode < finalMortonCode;
    };

    std::vector<eastl::fixed_vector<ChildDescriptor, 8>> queues(depth + 1);
    while (hasInput()) {
        ChildDescriptor l = consume();
        queues[depth].push_back(l);
        int d = depth;

        while (d > 0 && queues[d].full()) {
            // Store in allocator (as opposed to store to disk) and create new inner node
            auto baseIndex = storeDescriptors(queues[d]); // Store first so we know the base index (index of first child)
            ChildDescriptor p = makeInnerNode(baseIndex, queues[d]);

            queues[d].clear();
            queues[d - 1].push_back(p);
            d--;
        }
    }

    m_rootNode = queues[0][0];
}

static int floatAsInt(float v)
{
    static_assert(sizeof(float) == sizeof(unsigned));

    // Using reinterpret_cast might lead to undefined behavior because of illegal type aliasing, use memcpy instead
    // https://en.cppreference.com/w/cpp/language/reinterpret_cast#Type_aliasing
    unsigned r;
    std::memcpy(&r, &v, sizeof(float));
    return r;
}

static float intAsFloat(int v)
{
    static_assert(sizeof(float) == sizeof(int));

    // Using reinterpret_cast might lead to undefined behavior because of illegal type aliasing, use memcpy instead
    // https://en.cppreference.com/w/cpp/language/reinterpret_cast#Type_aliasing
    float r;
    std::memcpy(&r, &v, sizeof(float));
    return r;
}

std::optional<float> SparseVoxelOctree::intersect(Ray ray)
{
    // Based on the reference implementation of Efficient Sparse Voxel Octrees:
    // https://github.com/poelzi/efficient-sparse-voxel-octrees/blob/master/src/octree/cuda/Raycast.inl
    const int CAST_STACK_DEPTH = 23; //intLog2(m_resolution);

    // Get rid of small ray direction components to avoid division by zero
    const float epsilon = std::exp2f(-CAST_STACK_DEPTH);
    if (fabsf(ray.direction.x) < epsilon)
        ray.direction.x = copysignf(epsilon, ray.direction.x);
    if (fabsf(ray.direction.y) < epsilon)
        ray.direction.y = copysignf(epsilon, ray.direction.y);
    if (fabsf(ray.direction.z) < epsilon)
        ray.direction.z = copysignf(epsilon, ray.direction.z);

    // Precompute the coefficients of tx(x), ty(y) and tz(z).
    // The octree is assumed to reside at coordinates [1, 2].
    glm::vec3 tCoef = 1.0f / -glm::abs(ray.direction);
    glm::vec3 tBias = tCoef * ray.origin;

    // Select octant mask to mirro the coordinate system so taht ray direction is negative along each axis
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
    }

    // Initialize the current voxel to the first child of the root
    ChildDescriptor parent = m_rootNode;
    int idx = 0;
    glm::vec3 pos = glm::vec3(1.0f);
    int scale = CAST_STACK_DEPTH - 1;
    float scaleExp2 = 0.5f; // exp2f(scale - sMax)

    // Initialize the active span of t-values
    float tMin = maxComponent(2.0f * tCoef - tBias);
    float tMax = minComponent(tCoef - tBias);
    tMin = std::max(tMin, 0.0f);
    //tMax = std::min(tMax, 1.0f);

    if (tMin >= tMax)
        return {};

    // Intersection of ray (negative in all directions) with the root node (cube at [1, 2])
    if (1.5 * tCoef.x - tBias.x > tMin) {
        idx ^= (1 << 0);
        pos.x = 1.5f;
    }
    if (1.5 * tCoef.y - tBias.y > tMin) {
        idx ^= (1 << 1);
        pos.y = 1.5f;
    }
    if (1.5 * tCoef.z - tBias.z > tMin) {
        idx ^= (1 << 2);
        pos.z = 1.5f;
    }

    // Traverse voxels along the ray as long as the current voxel stays within the octree
    //ALWAYS_ASSERT(CAST_STACK_DEPTH < 16, "SVO too deep to traverse with a stack of size 16. Increase the stack size or use a shallower SVO.");
    struct StackItem {
        ChildDescriptor parent;
        float tMax;
    };
    std::array<StackItem, CAST_STACK_DEPTH + 1> stack;
    while (scale < CAST_STACK_DEPTH) {
        // === INTERSECT ===
        // Determine the maximum t-value of the cube by evaluating tx(), ty() and tz() at its corner
        glm::vec3 tCorner = pos * tCoef - tBias;
        float tcMax = minComponent(tCorner);

        // Process voxel if the corresponding bit in the valid mask is set
        int childIndex = 7 - idx ^ octantMask; // TODO: this might need to be 7 - childIndex
        if (parent.isValid(childIndex) && tMin <= tMax) {
            // === INTERSECT ===
            float tvMax = std::min(tMax, tcMax);
            float half = scaleExp2 * 0.5f;
            glm::vec3 tCenter = half * tCoef + tCorner;

            if (parent.isLeaf(childIndex)) {
                break; // Line 231
            }

            if (tMin <= tvMax) { // TODO: this is always true?
                // === PUSH ===
                stack[scale] = { parent, tMax };

                // Find child descriptor corresponding to the current voxel
                parent = getChild(parent, childIndex);

                // Select the child voxel that the ray enters first.
                idx = 0;
                scale--;
                scaleExp2 = half;
                if (tCenter.x > tMin) {
                    idx ^= (1 << 0);
                    pos.x += scaleExp2;
                }
                if (tCenter.y > tMin) {
                    idx ^= (1 << 1);
                    pos.y += scaleExp2;
                }
                if (tCenter.z > tMin) {
                    idx ^= (1 << 2);
                    pos.z += scaleExp2;
                }

                // Update active t-span
                tMax = tvMax;

                continue;
            }
        }

        // === ADVANCE ===

        // Step along the ray
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
        }

        // Update active t-span and flip bits of the child slot index
        tMin = tcMax;
        idx ^= stepMask;

        // Proceed with pop if the bit flip disagree with the ray direction
        if ((idx & stepMask) != 0) {
            // === POP ===
            // Find the highest differing bit between the two positions
            unsigned differingBits = 0;
            if ((stepMask & (1 << 0)) != 0) {
                differingBits |= floatAsInt(pos.x) ^ floatAsInt(pos.x + scaleExp2);
            }
            if ((stepMask & (1 << 1)) != 0) {
                differingBits |= floatAsInt(pos.y) ^ floatAsInt(pos.y + scaleExp2);
            }
            if ((stepMask & (1 << 2)) != 0) {
                differingBits |= floatAsInt(pos.z) ^ floatAsInt(pos.z + scaleExp2);
            }
            scale = (floatAsInt((float)differingBits) >> 23) - 127; // Position of the highest bit (complicated alternative to bitscan)
			float newVal = intAsFloat((scale - CAST_STACK_DEPTH + 127) << 23);
            scaleExp2 = newVal; // exp2f(scale - s_max)

            // Restore parent voxel from the stack
            parent = stack[scale].parent;
            tMax = stack[scale].tMax;

            // Round cube position and extract child slot index
            int shx = floatAsInt(pos.x) >> scale;
            int shy = floatAsInt(pos.y) >> scale;
            int shz = floatAsInt(pos.z) >> scale;
            pos.x = intAsFloat(shx << scale);
            pos.y = intAsFloat(shy << scale);
            pos.z = intAsFloat(shz << scale);
            idx = (shx & 1) | ((shy & 1) << 1) | ((shz & 1) << 2);
        }
    }

    // Indicate miss if we are outside the octree
	if (scale >= CAST_STACK_DEPTH) 		{
		return {};
	}

    /*// Undo mirroring of the coordinate system
    if ((octantMask & (1 << 0)) == 0)
        pos.x = 3.0f - scaleExp2 - pos.x;
    if ((octantMask & (1 << 1)) == 0)
        pos.y = 3.0f - scaleExp2 - pos.y;
    if ((octantMask & (1 << 2)) == 0)
        pos.z = 3.0f - scaleExp2 - pos.z;

	glm::vec3 hitPos = glm::min(glm::max(ray.origin + tMin * ray.direction, pos.x + epsilon), pos.x + scaleExp2 - epsilon);*/

    // Output result
	return tMin;
}

SparseVoxelOctree::ChildDescriptor SparseVoxelOctree::getChild(const ChildDescriptor& descriptor, int idx) const
{
    // TODO: use bitwise operation to get mask of valid child (non-leaf) nodes.
    // TODO: use the popcount instruction to determine the index into the allocator.
    int activeChildCount = 0;
    for (int i = 0; i < idx; i++) {
        if (descriptor.isValid(i) && !descriptor.isLeaf(i)) {
            activeChildCount++;
        }
    }
    return m_allocator[descriptor.childPtr + activeChildCount];
}

SparseVoxelOctree::ChildDescriptor SparseVoxelOctree::createStagingDescriptor(gsl::span<bool, 8> validMask, gsl::span<bool, 8> leafMask)
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
    ChildDescriptor descriptor;
    descriptor.childPtr = 0;
    descriptor.validMask = validMaskBits;
    descriptor.leafMask = leafMaskBits;
    return descriptor;
}

SparseVoxelOctree::ChildDescriptor SparseVoxelOctree::makeInnerNode(uint16_t baseIndex, gsl::span<ChildDescriptor, 8> children)
{
    std::array<bool, 8> validMask;
    std::array<bool, 8> leafMask;
    for (int i = 0; i < 8; i++) {
        const ChildDescriptor& child = children[i];
        if (child.isEmpty()) {
            // No children => empty
            validMask[i] = false;
            leafMask[i] = false;
        } else if (child.isFilled()) {
            // All children are leafs => leaf
            validMask[i] = true;
            leafMask[i] = true;
        } else {
            // One or more children and not 8 leaf children
            validMask[i] = true;
            leafMask[i] = false;
        }
    }
    auto descriptor = createStagingDescriptor(validMask, leafMask);
    descriptor.childPtr = baseIndex;
    return descriptor;
}

uint16_t SparseVoxelOctree::storeDescriptors(gsl::span<SparseVoxelOctree::ChildDescriptor> children)
{
    uint16_t baseIndex = static_cast<uint16_t>(m_allocator.size());
    for (const auto& descriptor : children) {
        if (!descriptor.isEmpty() && !descriptor.isFilled()) // Not all empty or all filled => inner node
        {
            m_allocator.push_back(descriptor);
        }
    }

    ALWAYS_ASSERT(m_allocator.size() < (1 << 16), "SVO to large, cannot use 16 bits to index");

    return baseIndex;
}

std::pair<std::vector<glm::vec3>, std::vector<glm::ivec3>> SparseVoxelOctree::generateSurfaceMesh() const
{
    std::vector<glm::vec3> positions;
    std::vector<glm::ivec3> triangles;

    struct StackItem {
        ChildDescriptor descriptor;
        glm::ivec3 start;
        int extent;
    };
    std::vector<StackItem> stack = { { m_rootNode, glm::ivec3(0), m_resolution } };
    while (!stack.empty()) {
        auto stackItem = stack.back();
        stack.pop_back();

        // Loop visit in morton order?
        int halfExtent = stackItem.extent / 2;

        int childID = 0;
        for (uint_fast32_t i = 0; i < 8; i++) {
            uint_fast16_t x, y, z;
            libmorton::morton3D_32_decode(i, x, y, z);
            glm::ivec3 cubeStart = stackItem.start + glm::ivec3(x * halfExtent, y * halfExtent, z * halfExtent);

            if (stackItem.descriptor.isValid(i)) {
                if (!stackItem.descriptor.isLeaf(i)) {
                    stack.push_back(StackItem { m_allocator[stackItem.descriptor.childPtr + childID++], cubeStart, halfExtent });
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

                    //if (halfExtent > 1)
                    //	continue;

                    glm::ivec3 offset((int)positions.size());
                    for (auto& q : quads) {
                        triangles.push_back(glm::ivec3 { q.x, q.y, q.z } + offset);
                        triangles.push_back(glm::ivec3 { q.x, q.z, q.w } + offset);
                    }

                    for (int i = 0; i < 24; ++i) {
                        positions.push_back(glm::vec3(cubeStart) + static_cast<float>(halfExtent) * cubePositions[i]);
                    }
                }
            }
        }
    }
    return { std::move(positions), std::move(triangles) };
}

}
