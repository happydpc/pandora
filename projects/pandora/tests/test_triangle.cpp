#include "pandora/geometry/triangle.h"
#include "pandora/samplers/uniform_sampler.h"
#include "gtest/gtest.h"
#include <random>
#include <thread>
#include <vector>

using namespace pandora;

#define MSVC (_MSC_VER && !__INTEL_COMPILER)

TEST(Triangle, Intersect1)
{
    std::vector<glm::ivec3> triangles = { glm::ivec3(0, 1, 2) };
    std::vector<glm::vec3> positions = { glm::vec3(-2, 0, 0), glm::vec3(0, 2, 0), glm::vec3(2, 0, 0) };

    TriangleMesh mesh(std::move(triangles), std::move(positions), {}, {}, {});
    {
        Ray ray = Ray(glm::vec3(0, 0, -1), glm::vec3(0, 0, 1));
        RayHit rayHit;
        ASSERT_TRUE(mesh.intersectPrimitive(ray, rayHit, 0));
    }
    {
        Ray ray = Ray(glm::vec3(0, 0, 1), glm::vec3(0, 0, -1));
        RayHit rayHit;
        ASSERT_TRUE(mesh.intersectPrimitive(ray, rayHit, 0));
    }
    {
        Ray ray = Ray(glm::vec3(0, 3, -1), glm::vec3(0, 0, 1));
        RayHit rayHit;
        ASSERT_FALSE(mesh.intersectPrimitive(ray, rayHit, 0));
    }
    {
        Ray ray = Ray(glm::vec3(-1.0f, -0.5f, -1), glm::normalize(glm::vec3(1, 1, 1)));
        RayHit rayHit;
        ASSERT_TRUE(mesh.intersectPrimitive(ray, rayHit, 0));
    }

    {
        Ray ray = Ray(glm::vec3(0.99f, 0.99f, -1), glm::vec3(0, 0, 1));
        RayHit rayHit;
        ASSERT_TRUE(mesh.intersectPrimitive(ray, rayHit, 0));
    }
}

TEST(Triangle, AreaCorrect)
{
    std::vector<glm::ivec3> triangles = { glm::ivec3(0, 1, 2) };
    std::vector<glm::vec3> positions = { glm::vec3(-2, 0, 0), glm::vec3(0, 0, 2), glm::vec3(2, 0, 0) };

    TriangleMesh mesh(std::move(triangles), std::move(positions), {}, {}, {});

    float scanLine = 1.0f;
    int totalSampleCount = 0;
    int samplesHit = 0;
    for (float x = -2.0f; x < 2.0f; x += 0.01f) {
        Ray ray = Ray(glm::vec3(x, -1, scanLine), glm::vec3(0, 1, 0));
        RayHit rayHit;
        if (mesh.intersectPrimitive(ray, rayHit, 0))
            samplesHit++;
        totalSampleCount++;
    }
    ASSERT_EQ(samplesHit, totalSampleCount / 2);
}

TEST(Triangle, Sample)
{
	UniformSampler sampler(1);
    std::vector<glm::ivec3> triangles = { glm::ivec3(0, 1, 2), glm::ivec3(3, 4, 5) };
	std::vector<glm::vec3> positions;
	for (int i = 0; i < 6; i++) {
		positions.push_back(glm::vec3(sampler.get1D(), sampler.get1D(), sampler.get1D()));
	}

    TriangleMesh mesh(std::move(triangles), std::move(positions), {}, {}, {});

	for (int p = 0; p < 2; p++)
	{
		for (int i = 0; i < 100; i++) {
			auto interaction = mesh.samplePrimitive(p, sampler.get2D());
			ASSERT_FLOAT_EQ(glm::length(interaction.normal), 1.0f);

			Ray ray = Ray(interaction.position - interaction.normal, interaction.normal);
            RayHit rayHit;
			ASSERT_TRUE(mesh.intersectPrimitive(ray, rayHit, p));
		}
	}
}

TEST(Triangle, SampleFromReference)
{
	UniformSampler sampler(1);
	std::vector<glm::ivec3> triangles = { glm::ivec3(0, 1, 2), glm::ivec3(3, 4, 5) };
	std::vector<glm::vec3> positions;
	for (int i = 0; i < 6; i++) {
		positions.push_back(glm::vec3(sampler.get1D(), sampler.get1D(), sampler.get1D()));
	}

    TriangleMesh mesh(std::move(triangles), std::move(positions), {}, {}, {});

	Interaction ref;
	ref.position = glm::vec3(-23, 45, 34);
	ref.wo = glm::normalize(glm::vec3(1, 3, -2));

	for (int p = 0; p < 2; p++)
	{
		for (int i = 0; i < 100; i++) {
			auto interaction = mesh.samplePrimitive(p, ref, sampler.get2D());
			ref.normal = glm::normalize(interaction.position - ref.position);// Normal oriented towards the light
			ASSERT_FLOAT_EQ(glm::length(interaction.normal), 1.0f);

			Ray ray = ref.spawnRay(glm::normalize(interaction.position - ref.position));
            RayHit rayHit;
			ASSERT_TRUE(mesh.intersectPrimitive(ray, rayHit, p));

            if (rayHit.primitiveID != (unsigned)-1) {
			    ASSERT_NE(mesh.pdfPrimitive(p, ref, glm::normalize(interaction.position - ref.position)), 0.0f);
            }
		}
	}
}
