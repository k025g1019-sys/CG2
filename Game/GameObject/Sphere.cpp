#include "Sphere.h"
#include "Vector3.h"
#include "VertexData.h"
#include <cmath>

void Sphere::Generate(int subdivision) {
    vertices_.clear();

    const float pi = 3.1415926535f;

    float lonStep =
        2.0f * pi / subdivision;

    float latStep =
        pi / subdivision;

    for (int lat = 0; lat < subdivision; ++lat) {

        float lat0 =
            -pi / 2.0f + lat * latStep;

        float lat1 =
            -pi / 2.0f + (lat + 1) * latStep;

        for (int lon = 0; lon < subdivision; ++lon) {

            float lon0 = lon * lonStep;
            float lon1 = (lon + 1) * lonStep;

            Vector3 p0 = {
                cosf(lat0) * cosf(lon0),
                sinf(lat0),
                cosf(lat0) * sinf(lon0)
            };

            Vector3 p1 = {
                cosf(lat1) * cosf(lon0),
                sinf(lat1),
                cosf(lat1) * sinf(lon0)
            };

            Vector3 p2 = {
                cosf(lat1) * cosf(lon1),
                sinf(lat1),
                cosf(lat1) * sinf(lon1)
            };

            Vector3 p3 = {
                cosf(lat0) * cosf(lon1),
                sinf(lat0),
                cosf(lat0) * sinf(lon1)
            };

            VertexData v0{ {p0.x,p0.y,p0.z,1}, {0,1} };
            VertexData v1{ {p1.x,p1.y,p1.z,1}, {0,0} };
            VertexData v2{ {p2.x,p2.y,p2.z,1}, {1,0} };
            VertexData v3{ {p3.x,p3.y,p3.z,1}, {1,1} };

            vertices_.push_back(v0);
            vertices_.push_back(v1);
            vertices_.push_back(v2);

            vertices_.push_back(v0);
            vertices_.push_back(v2);
            vertices_.push_back(v3);
        }
    }
}