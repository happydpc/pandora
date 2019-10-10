#include "pandora/integrators/normal_debug_integrator.h"
#include "pandora/core/stats.h"
#include "pandora/graphics_core/interaction.h"
#include "pandora/graphics_core/perspective_camera.h"
#include "pandora/graphics_core/ray.h"

namespace pandora {

NormalDebugIntegrator::NormalDebugIntegrator(
    tasking::TaskGraph* pTaskGraph)
    : m_pTaskGraph(pTaskGraph)
    , m_hitTask(
          pTaskGraph->addTask<std::tuple<Ray, RayHit, RayState>>(
              [this](gsl::span<const std::tuple<Ray, RayHit, RayState>> hits, std::pmr::memory_resource* pMemoryResource) {
                  for (const auto& [ray, rayHit, state] : hits) {
                      SurfaceInteraction si;
                      si.sceneObject = rayHit.sceneObjectRef;
                      si.normal = rayHit.geometricNormal;
                      si.uv = rayHit.geometricUV;
                      this->rayHit(ray, si, state);
                  }
              }))
    , m_missTask(
          pTaskGraph->addTask<std::tuple<Ray, RayState>>(
              [this](gsl::span<const std::tuple<Ray, RayState>> misses, std::pmr::memory_resource* pMemoryResource) {
                  for (const auto& [ray, state] : misses) {
                      this->rayMiss(ray, state);
                  }
              }))
    , m_anyHitTask(
          pTaskGraph->addTask<std::tuple<Ray, RayHit, AnyRayState>>(
              [this](gsl::span<const std::tuple<Ray, RayHit, AnyRayState>> hits, std::pmr::memory_resource* pMemoryResource) {
                  for (const auto& [ray, rayHit, state] : hits) {
                      this->rayAnyHit(ray, rayHit, state);
                  }
              }))
    , m_anyMissTask(
          pTaskGraph->addTask<std::tuple<Ray, AnyRayState>>(
              [this](gsl::span<const std::tuple<Ray, AnyRayState>> misses, std::pmr::memory_resource* pMemoryResource) {
                  for (const auto& [ray, state] : misses) {
                      this->rayAnyMiss(ray, state);
                  }
              }))
{
}

void NormalDebugIntegrator::render(const PerspectiveCamera& camera, Sensor& sensor)
{
    m_pCamera = &camera;
    m_pSensor = &sensor;
    m_currentRayIndex.store(0);
    m_resolution = sensor.getResolution();
    m_maxPixelIndex = m_resolution.x * m_resolution.y;

    // Spawn initial rays
    spawnNewPaths(500 * 1000);
    m_pTaskGraph->run();
}

void NormalDebugIntegrator::rayHit(const Ray& ray, const SurfaceInteraction& si, const RayState& state)
{
    m_pSensor->addPixelContribution(state.pixel, glm::abs(glm::normalize(si.normal)));
}

void NormalDebugIntegrator::rayMiss(const Ray& ray, const RayState& state)
{
}

void NormalDebugIntegrator::rayAnyHit(const Ray& ray, const RayHit& rayHit, const AnyRayState& state)
{
}

void NormalDebugIntegrator::rayAnyMiss(const Ray& ray, const AnyRayState& state)
{
}

void NormalDebugIntegrator::spawnNewPaths(int numPaths)
{
    int startIndex = m_currentRayIndex.fetch_add(numPaths);
    int endIndex = std::min(startIndex + numPaths, m_maxPixelIndex);

    for (int pixelIndex = startIndex; pixelIndex < endIndex; pixelIndex++) {
        int x = pixelIndex % m_resolution.x;
        int y = pixelIndex / m_resolution.x;

        CameraSample cameraSample;
        cameraSample.pixel = { x, y };

        m_pCamera->generateRay(cameraSample);
    }
}

NormalDebugIntegrator::HitTaskHandle NormalDebugIntegrator::hitTaskHandle() const
{
    return m_hitTask;
}

NormalDebugIntegrator::MissTaskHandle NormalDebugIntegrator::missTaskHandle() const
{
    return m_missTask;
}

NormalDebugIntegrator::AnyHitTaskHandle NormalDebugIntegrator::anyHitTaskHandle() const
{
    return m_anyHitTask;
}

NormalDebugIntegrator::AnyMissTaskHandle NormalDebugIntegrator::anyMissTaskHandle() const
{
    return m_anyMissTask;
}

}
