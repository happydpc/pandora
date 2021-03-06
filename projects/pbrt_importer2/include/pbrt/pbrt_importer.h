#pragma once
#include <filesystem>
#include <pandora/graphics_core/render_config.h>
#include <stream/cache/cache.h>

namespace pbrt {

pandora::RenderConfig loadFromPBRTFile(std::filesystem::path filePath, unsigned cameraID, tasking::CacheBuilder* pCacheBuilder = nullptr, unsigned subdiv = 0, bool loadTextures = false);

}