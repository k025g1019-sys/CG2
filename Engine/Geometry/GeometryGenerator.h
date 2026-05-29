#pragma once
#include <vector>
#include "VertexData.h"

class GeometryGenerator {
public:
    static void GenerateSphere(uint32_t subdivision, std::vector<VertexData>& out);
};