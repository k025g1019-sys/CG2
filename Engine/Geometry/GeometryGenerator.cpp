#include "GeometryGenerator.h"
#include <cmath>
#include "Vector3.h"

void GeometryGenerator::GenerateSphere(uint32_t sub, std::vector<VertexData>& out) {
    const float pi = 3.1415926535f;

    out.resize(sub * sub * 6);

    float dLon = 2 * pi / sub;
    float dLat = pi / sub;

    for (uint32_t y = 0; y < sub; y++) {
        float lat0 = -pi / 2 + y * dLat;
        float lat1 = lat0 + dLat;

        for (uint32_t x = 0; x < sub; x++) {
            float lon0 = x * dLon;
            float lon1 = lon0 + dLon;

            uint32_t i = (y * sub + x) * 6;

            Vector3 v0{ cos(lat0) * cos(lon0), sin(lat0), cos(lat0) * sin(lon0) };
            Vector3 v1{ cos(lat1) * cos(lon0), sin(lat1), cos(lat1) * sin(lon0) };
            Vector3 v2{ cos(lat0) * cos(lon1), sin(lat0), cos(lat0) * sin(lon1) };
            Vector3 v3{ cos(lat1) * cos(lon1), sin(lat1), cos(lat1) * sin(lon1) };

            out[i + 0].position = { v0.x,v0.y,v0.z,1 };
            out[i + 1].position = { v1.x,v1.y,v1.z,1 };
            out[i + 2].position = { v2.x,v2.y,v2.z,1 };

            out[i + 3].position = { v2.x,v2.y,v2.z,1 };
            out[i + 4].position = { v1.x,v1.y,v1.z,1 };
            out[i + 5].position = { v3.x,v3.y,v3.z,1 };
        }
    }
}