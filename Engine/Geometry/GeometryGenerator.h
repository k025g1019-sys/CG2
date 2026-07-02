#pragma once

#include <cstdint>
#include <vector>

#include "Engine/Rendering/VertexData.h"

// 緯度経度分割の球（半径1・原点中心）の頂点列を生成する
std::vector<VertexData> GenerateSphereVertices(uint32_t subdivision);
