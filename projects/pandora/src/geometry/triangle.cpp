#include "pandora/geometry/triangle.h"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "glm/mat4x4.hpp"
#include "pandora/utility/math.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <stack>
#include <tuple>

static bool fileExists(const std::string_view name)
{
    std::ifstream f(name.data());
    return f.good() && f.is_open();
}

static glm::mat4 assimpMatrix(const aiMatrix4x4& m)
{
    //float values[3][4] = {};
    glm::mat4 matrix;
    matrix[0][0] = m.a1;
    matrix[0][1] = m.b1;
    matrix[0][2] = m.c1;
    matrix[0][3] = m.d1;
    matrix[1][0] = m.a2;
    matrix[1][1] = m.b2;
    matrix[1][2] = m.c2;
    matrix[1][3] = m.d2;
    matrix[2][0] = m.a3;
    matrix[2][1] = m.b3;
    matrix[2][2] = m.c3;
    matrix[2][3] = m.d3;
    matrix[3][0] = m.a4;
    matrix[3][1] = m.b4;
    matrix[3][2] = m.c4;
    matrix[3][3] = m.d4;
    return matrix;
}

static glm::vec3 assimpVec(const aiVector3D& v)
{
    return glm::vec3(v.x, v.y, v.z);
}

namespace pandora {

TriangleMesh::TriangleMesh(
    unsigned numTriangles,
    unsigned numVertices,
    std::unique_ptr<glm::ivec3[]>&& triangles,
    std::unique_ptr<glm::vec3[]>&& positions,
    std::unique_ptr<glm::vec3[]>&& normals,
    std::unique_ptr<glm::vec3[]>&& tangents,
    std::unique_ptr<glm::vec2[]>&& uvCoords)
    : m_numTriangles(numTriangles)
    , m_numVertices(numVertices)
    , m_triangles(std::move(triangles))
    , m_positions(std::move(positions))
    , m_normals(std::move(normals))
    , m_tangents(std::move(tangents))
    , m_uvCoords(std::move(uvCoords))
{
}

std::pair<std::shared_ptr<TriangleMesh>, std::shared_ptr<Material>> TriangleMesh::createMeshAssimp(const aiScene* scene, const unsigned meshIndex, const glm::mat4& transform)
{
    const aiMesh* mesh = scene->mMeshes[meshIndex];

    if (mesh->mNumVertices == 0 || mesh->mNumFaces == 0)
        throw std::runtime_error("Empty mesh");

    auto indices = std::make_unique<glm::ivec3[]>(mesh->mNumFaces);
    auto positions = std::make_unique<glm::vec3[]>(mesh->mNumVertices);
    auto normals = std::make_unique<glm::vec3[]>(mesh->mNumVertices); // Shading normals
    //auto tangents = std::make_unique<glm::vec3[]>(mesh->mNumVertices); // Shading tangents
    std::unique_ptr<glm::vec3[]> tangents = nullptr;
    std::unique_ptr<glm::vec2[]> uvCoords = nullptr;

    // Triangles
    for (unsigned i = 0; i < mesh->mNumFaces; i++) {
        const aiFace& face = mesh->mFaces[i];
        if (face.mNumIndices != 3) {
            throw std::runtime_error("Found a face which is not a triangle, discarding!");
        }

        auto aiIndices = face.mIndices;
        indices[i] = { aiIndices[0], aiIndices[1], aiIndices[2] };
    }

    // Positions
    for (unsigned i = 0; i < mesh->mNumVertices; i++) {
        positions[i] = transform * glm::vec4(assimpVec(mesh->mVertices[i]), 1);
    }

    // Normals & tangents
    glm::mat3 normalTransform = transform;
    for (unsigned i = 0; i < mesh->mNumVertices; i++) {
        normals[i] = normalTransform * glm::vec3(assimpVec(mesh->mNormals[i]));
        //tangents[i] = normalTransform * glm::vec3(assimpVec(mesh->mTangents[i]));
    }

    /*// UV mapping
    if (mesh->HasTextureCoords(0)) {
        uvCoords = std::make_unique<glm::vec2[]>(mesh->mNumVertices);
        for (unsigned i = 0; i < mesh->mNumVertices; i++) {
            uvCoords[i] = glm::vec2(assimpVec(mesh->mTextureCoords[0][i]));
        }
    }*/

    return { std::make_shared<TriangleMesh>(mesh->mNumFaces, mesh->mNumVertices, std::move(indices), std::move(positions), std::move(normals), std::move(tangents), std::move(uvCoords)), nullptr };
}

std::vector<std::pair<std::shared_ptr<TriangleMesh>, std::shared_ptr<Material>>> TriangleMesh::loadFromFile(const std::string_view filename, glm::mat4 modelTransform)
{
    if (!fileExists(filename)) {
        std::cout << "Could not find mesh file: " << filename << std::endl;
        return {};
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filename.data(), aiProcessPreset_TargetRealtime_MaxQuality);
    //importer.ApplyPostProcessing(aiProcess_CalcTangentSpace);

    if (scene == nullptr || scene->mRootNode == nullptr || scene->mFlags == AI_SCENE_FLAGS_INCOMPLETE) {
        std::cout << "Failed to load mesh file: " << filename << std::endl;
        return {};
    }

    std::vector<std::pair<std::shared_ptr<TriangleMesh>, std::shared_ptr<Material>>> result;

    std::stack<std::tuple<aiNode*, glm::mat4>> stack;
    stack.push({ scene->mRootNode, modelTransform * assimpMatrix(scene->mRootNode->mTransformation) });
    while (!stack.empty()) {
        auto [node, transform] = stack.top();
        stack.pop();

        transform *= assimpMatrix(node->mTransformation);

        for (unsigned i = 0; i < node->mNumMeshes; i++) {
            // Process subMesh
            result.push_back(createMeshAssimp(scene, node->mMeshes[i], transform));
        }

        for (unsigned i = 0; i < node->mNumChildren; i++) {
            stack.push({ node->mChildren[i], transform });
        }
    }

    return result;
}

unsigned TriangleMesh::numTriangles() const
{
    return m_numTriangles;
}

unsigned TriangleMesh::numVertices() const
{
    return m_numVertices;
}

gsl::span<const glm::ivec3> TriangleMesh::getTriangles() const
{
    return gsl::make_span(m_triangles.get(), m_numTriangles);
}

gsl::span<const glm::vec3> TriangleMesh::getPositions() const
{
    return gsl::make_span(m_positions.get(), m_numVertices);
}

SurfaceInteraction TriangleMesh::partialFillSurfaceInteraction(unsigned primID, const glm::vec2& embreeUV) const
{
    // Barycentric coordinates
    float b0 = 1.0f - embreeUV.x - embreeUV.y;
    float b1 = embreeUV.x;
    float b2 = embreeUV.y;
    assert(b0 + b1 <= 1.0f);

    glm::vec3 dpdu, dpdv;
    glm::vec2 uv[3];
    getUVs(primID, uv);
    glm::vec3 p[3];
    getPs(primID, p);
    // Compute deltas for triangle partial derivatives
    glm::vec2 duv02 = uv[0] - uv[2], duv12 = uv[1] - uv[2];
    glm::vec3 dp02 = p[0] - p[2], dp12 = p[1] - p[2];

    float determinant = duv02[0] * duv12[1] - duv02[1] * duv12[0];
    if (determinant == 0.0f) {
        // Handle zero determinant for triangle partial derivative matrix
        coordinateSystem(glm::normalize(glm::cross(p[2] - p[0], p[1] - p[0])), &dpdu, &dpdv);
    } else {
        float invDet = 1.0f / determinant;
        dpdu = (duv12[1] * dp02 - duv02[1] * dp12) * invDet;
        dpdv = (-duv12[0] * dp02 + duv02[0] * dp12) * invDet;
    }

    glm::vec3 dndu, dndv;
    dndu = dndv = glm::vec3(0.0f);

    glm::vec3 hitP = b0 * p[0] + b1 * p[1] + b2 * p[2];
    glm::vec2 hitUV = b0 * uv[0] + b1*uv[1] + b2 * uv[2];

    SurfaceInteraction si;
    si.position = hitP;//p[0] + embreeUV.x * (p[1] - p[0]) + embreeUV.y * (p[2] - p[0]); // Should be considerably more accurate than ray.o + t * ray.d
    si.uv = hitUV;
    si.dpdu = dpdu;
    si.dpdv = dpdv;
    si.normal = si.shading.normal = glm::normalize(glm::cross(dp02, dp12));
    si.dndu = dndu;
    si.dndv = dndv;

    // Shading normals / tangents
    if (m_normals || m_tangents) {
        glm::ivec3 v = m_triangles[primID];

        glm::vec3 ns;
        if (m_normals)
            ns = glm::normalize(b0 * m_normals[v[0]] + b1 * m_normals[v[1]] + b2 * m_normals[v[2]]);
        else
            ns = si.normal;

        glm::vec3 ss;
        if (m_tangents)
            ss = glm::normalize(b0 * m_tangents[v[0]] + b1 * m_tangents[v[1]] + b2 * m_tangents[v[2]]);
        else
            ss = glm::normalize(si.dpdu);

        glm::vec3 ts = glm::cross(ss, ns);
        if (glm::dot(ts, ts) > 0.0f) { // glm::dot(ts, ts) = length2(ts)
            ts = glm::normalize(ts);
            ss = glm::cross(ts, ns);
        } else {
            coordinateSystem(ns, &ss, &ts);
        }

        si.setShadingGeometry(ss, ts, dndu, dndv, true);
    }

    // Ensure correct orientation of the geometric normal
    if (m_normals)
        si.normal = faceForward(si.normal, si.shading.normal);

    return si;
}

float TriangleMesh::primitiveArea(unsigned primitiveID) const
{
    const auto& triangle = m_triangles[primitiveID];
    const glm::vec3& p0 = m_positions[triangle[0]];
    const glm::vec3& p1 = m_positions[triangle[1]];
    const glm::vec3& p2 = m_positions[triangle[2]];
    return 0.5f * glm::cross(p1 - p0, p2 - p0).length();
}

std::pair<Interaction, float> TriangleMesh::samplePrimitive(unsigned primitiveID, const glm::vec2& randomSample) const
{
    // Compute uniformly sampled barycentric coordinates
    // https://github.com/mmp/pbrt-v3/blob/master/src/shapes/triangle.cpp
    float su0 = std::sqrt(randomSample[0]);
    glm::vec2 b = glm::vec2(1 - su0, randomSample[1] * su0);

    const auto& triangle = m_triangles[primitiveID];
    const glm::vec3& p0 = m_positions[triangle[0]];
    const glm::vec3& p1 = m_positions[triangle[1]];
    const glm::vec3& p2 = m_positions[triangle[2]];

    Interaction it;
    it.position = b[0] * p0 + b[1] * p1 + (1 - b[0] - b[1]) * p2;
    it.normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));

    float pdf = 1.0f / primitiveArea(primitiveID);

    return { it, pdf };
}

std::pair<Interaction, float> TriangleMesh::samplePrimitive(unsigned primitiveID, const Interaction& ref, const glm::vec2& randomSample) const
{
    (void)ref;
    return samplePrimitive(primitiveID, randomSample);
}

void TriangleMesh::getUVs(unsigned primitiveID, gsl::span<glm::vec2, 3> uv) const
{
    if (m_uvCoords) {
        glm::ivec3 indices = m_triangles[primitiveID];
        uv[0] = m_uvCoords[indices[0]];
        uv[1] = m_uvCoords[indices[1]];
        uv[2] = m_uvCoords[indices[2]];
    } else {
        uv[0] = glm::vec2(0, 0);
        uv[1] = glm::vec2(1, 0);
        uv[2] = glm::vec2(1, 1);
    }
}

void TriangleMesh::getPs(unsigned primitiveID, gsl::span<glm::vec3, 3> p) const
{
    glm::ivec3 indices = m_triangles[primitiveID];
    p[0] = m_positions[indices[0]];
    p[1] = m_positions[indices[1]];
    p[2] = m_positions[indices[2]];
}

}

/*
bool TriangleMesh::intersectMollerTrumbore(unsigned int primitiveIndex, Ray& ray) const
{
    // https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
    const float EPSILON = 0.000001f;

    Triangle triangle = m_triangles[primitiveIndex];
    glm::vec3 p0 = m_positions[triangle.i0];
    glm::vec3 p1 = m_positions[triangle.i1];
    glm::vec3 p2 = m_positions[triangle.i2];

    glm::vec3 e1 = p1 - p0;
    glm::vec3 e2 = p2 - p0;
    glm::vec3 h = cross(ray.direction, e2);
    float a = dot(e1, h);
    if (a > -EPSILON && a < EPSILON)
        return false;

    float f = 1.0f / a;
    glm::vec3 s = ray.origin - p0;
    float u = f * dot(s, h);
    if (u < 0.0f || u > 1.0f)
        return false;

    glm::vec3 q = cross(s, e1);
    float v = f * dot(ray.direction, q);
    if (v < 0.0f || u + v > 1.0f)
        return false;

    float t = f * dot(e2, q);
    if (t > EPSILON) {
        ray.t = t;
        ray.uv = glm::vec2(u, v);
        return true;
    }

    return false;
}

bool TriangleMesh::intersectPbrt(unsigned primitiveIndex, Ray& ray) const
{
    // Based on PBRT v3 triangle intersection test:
    // https://github.com/mmp/pbrt-v3/blob/master/src/shapes/triangle.cpp
    //
    // Transform the ray and triangle such that the ray origin is at (0,0,0) and its
    // direction points along the +Z axis. This makes the intersection test easy and
    // allows for watertight intersection testing.
    Triangle triangle = m_triangles[primitiveIndex];
    glm::vec3 p0 = m_positions[triangle.i0];
    glm::vec3 p1 = m_positions[triangle.i1];
    glm::vec3 p2 = m_positions[triangle.i2];

    // Translate vertices based on ray origin
    glm::vec3 p0t = p0 - ray.origin;
    glm::vec3 p1t = p1 - ray.origin;
    glm::vec3 p2t = p2 - ray.origin;

    // Permutate components of triangle vertices and ray direction
    int kz = maxDimension(abs(ray.direction));
    int kx = kz + 1;
    if (kx == 3)
        kx = 0;
    int ky = kx + 1;
    if (ky == 3)
        ky = 0;
    //int kx = (kz + 1) % 3;
    //int ky = (kz + 2) % 3;
    glm::vec3 d = permute(ray.direction, kx, ky, kz);
    p0t = permute(p0t, kx, ky, kz);
    p1t = permute(p1t, kx, ky, kz);
    p2t = permute(p2t, kx, ky, kz);

    // Apply shear transformation to translated vertex positions
    // Aligns the ray direction with the +z axis. Only shear x and y dimensions,
    // we can wait and shear the z dimension only if the ray actually intersects
    // the triangle.
    //TODO(Mathijs): consider precomputing and storing the shear values in the ray
    float Sx = -d.x / d.z;
    float Sy = -d.y / d.z;
    float Sz = 1.0f / d.z;
    p0t.x += Sx * p0t.z;
    p0t.y += Sy * p0t.z;
    p1t.x += Sx * p1t.z;
    p1t.y += Sy * p1t.z;
    p2t.x += Sx * p2t.z;
    p2t.y += Sy * p2t.z;

    // Compute the edge function coefficients
    float e0 = p1t.x * p2t.y - p1t.y * p2t.x;
    float e1 = p2t.x * p0t.y - p2t.y * p0t.x;
    float e2 = p0t.x * p1t.y - p0t.y * p1t.x;

    // Fall back to double precision test at triangle edges
    if (e0 == 0.0f || e1 == 0.0f || e2 == 0.0f) {
        double p2txp1ty = (double)p2t.x * (double)p1t.y;
        double p2typ1tx = (double)p2t.y * (double)p1t.x;
        e0 = (float)(p2typ1tx - p2txp1ty);
        double p0txp2ty = (double)p0t.x * (double)p2t.y;
        double p0typ2tx = (double)p0t.y * (double)p2t.x;
        e1 = (float)(p0typ2tx - p0txp2ty);
        double p1txp0ty = (double)p1t.x * (double)p0t.y;
        double p1typ0tx = (double)p1t.y * (double)p0t.x;
        e2 = (float)(p1typ0tx - p1txp0ty);
    }

    // If the signs of the edge function values differ, then the point (0, 0) is not
    // on the same side of all three edges and therefor is outside the triangle.
    if ((e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) && ((e0 > 0.0f || e1 > 0.0f || e2 > 0.0f)))
        return false;

    // If the sum of the three ege function values is zero, then the ray is
    // approaching the triangle edge-on, and we report no intersection.
    float det = e0 + e1 + e2;
    if (det == 0.0f)
        return false;

    // Compute scaled hit distance to triangle and test against t range
    p0t.z *= Sz;
    p1t.z *= Sz;
    p2t.z *= Sz;
    float tScaled = e0 * p0t.z + e1 * p1t.z + e2 * p2t.z;
    if (det < 0.0f && (tScaled >= 0.0f || tScaled < ray.t * det))
        return false;
    else if (det > 0.0f && (tScaled <= 0.0f || tScaled > ray.t * det))
        return false;

    // Compute barycentric coordinates and t value for triangle intersection
    float invDet = 1.0f / det;
    float b0 = e0 * invDet;
    float b1 = e1 * invDet;
    //float b2 = e2 * invDet;
    float t = tScaled * invDet;

    ray.t = t;
    ray.uv = glm::vec2(b0, b1);

    return true;
}*/
