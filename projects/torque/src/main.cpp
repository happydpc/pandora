#include "pandora/core/load_from_file.h"
#include "pandora/integrators/direct_lighting_integrator.h"
#include "pandora/integrators/naive_direct_lighting_integrator.h"
#include "pandora/integrators/path_integrator.h"
#include "pandora/integrators/svo_depth_test_integrator.h"
#include "pandora/integrators/svo_test_integrator.h"
#include "output.h"
#include <xmmintrin.h>

#include <boost/program_options.hpp>
#include <string>
#include <tbb/tbb.h>
#include <xmmintrin.h>

using namespace pandora;
using namespace torque;
using namespace std::string_literals;

const std::string projectBasePath = "../../"s;

int main(int argc, char** argv)
{
    // https://embree.github.io/api.html
    // For optimal Embree performance
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

    namespace po = boost::program_options;

    po::options_description desc("Pandora options");
    desc.add_options()
        ("file", po::value<std::string>()->required(), "Pandora scene description JSON")
        ("out", po::value<std::string>()->default_value("output"s), "output name (without file extension!)")
        ("integrator", po::value<std::string>()->default_value("direct"), "integrator (direct or path)")
        ("spp", po::value<int>()->default_value(1), "samples per pixel")
        ("help", "show all arguments");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    try {
        po::notify(vm);
    }
    catch (const boost::program_options::required_option& e) {
        std::cout << "Missing required argument \"" << e.get_option_name() << "\"" << std::endl;
        return 1;
    }

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    std::cout << "Rendering with the following settings:\n";
    std::cout << "  file          " << vm["file"].as<std::string>() << "\n";
    std::cout << "  out           " << vm["out"].as<std::string>() << "\n";
    std::cout << "  integrator    " << vm["integrator"].as<std::string>() << "\n";
    std::cout << "  spp           " << vm["spp"].as<int>() << std::endl;

    auto renderConfig = loadFromFileOOC(vm["file"].as<std::string>(), false);

    /*// Skydome
    auto colorTexture = std::make_shared<ImageTexture<Spectrum>>(projectBasePath + "assets/skydome/DF360_005_Ref.hdr");
    auto transform = glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
    scene.addInfiniteLight(std::make_shared<EnvironmentLight>(transform, Spectrum(0.5f), 1, colorTexture));*/

    //scene.splitLargeSceneObjects(IN_CORE_BATCHING_PRIMS_PER_LEAF);

    auto integratorType = vm["integrator"].as<std::string>();
    int spp = vm["spp"].as<int>();
    if (integratorType == "direct") {
        DirectLightingIntegrator integrator(8, renderConfig.scene, renderConfig.camera->getSensor(), spp, LightStrategy::UniformSampleOne);
        integrator.startNewFrame();
        integrator.render(*renderConfig.camera);
    } else if (integratorType == "path") {
        PathIntegrator integrator(10, renderConfig.scene, renderConfig.camera->getSensor(), spp);
        integrator.startNewFrame();
        integrator.render(*renderConfig.camera);
    }
    //NaiveDirectLightingIntegrator integrator(8, scene, camera.getSensor(), spp);
    //SVOTestIntegrator integrator(scene, camera.getSensor(), spp);
    //SVODepthTestIntegrator integrator(scene, camera.getSensor(), spp);

    writeOutputToFile(renderConfig.camera->getSensor(), spp, vm["out"].as<std::string>() + ".jpg", true);
    writeOutputToFile(renderConfig.camera->getSensor(), spp, vm["out"].as<std::string>() + ".exr", false);

    return 0;
}
