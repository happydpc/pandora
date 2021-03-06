#pragma once
#include "pandora/graphics_core/spectrum.h"
#include <glm/glm.hpp>

namespace pandora {

// Forward declares
struct Interaction;
struct SurfaceInteraction;
struct LightSample;
class Transform;

//class InMemoryResource;
//template <typename T>
//class FifoCache;
//template <typename... T>
//class LRUCache;

class Light;
class InfiniteLight;
class DistantLight;
class EnvironmentLight;
class AreaLight;

class BxDF;
class BSDF;
class Material;
class MatteMaterial;
class MirrorMaterial;

class Sampler;
class UniformSampler;

template <class T>
class Texture;
template <class T>
class ConstantTexture;
template <class T>
class ImageTexture;

struct Scene;
struct SceneNode;
struct SceneObject;
class Shape;
class TriangleShape;
struct Bounds;
struct Ray;
struct RayHit;

class MemoryArena;
class MemoryArenaTS;

class PathIntegrator;
class PerspectiveCamera;
class Sensor;

class VoxelGrid;
class SparseVoxelOctree;

}
