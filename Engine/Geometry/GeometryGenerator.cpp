#include "Engine/Geometry/GeometryGenerator.h"

#include <cmath>

#include "Engine/Math/Vector3.h"

std::vector<VertexData> GenerateSphereVertices(uint32_t subdivision) {
	const float pi = 3.1415926535f;

	std::vector<VertexData> vertices;
	vertices.resize(size_t(subdivision) * subdivision * 6);

	const float kLonEvery = 2.0f * pi / subdivision;
	const float kLatEvery = pi / subdivision;

	for (uint32_t latIndex = 0; latIndex < subdivision; ++latIndex) {
		float lat0 = -pi / 2.0f + kLatEvery * latIndex;
		float lat1 = lat0 + kLatEvery;

		for (uint32_t lonIndex = 0; lonIndex < subdivision; ++lonIndex) {
			float lon0 = lonIndex * kLonEvery;
			float lon1 = (lonIndex + 1) * kLonEvery;

			uint32_t start = (latIndex * subdivision + lonIndex) * 6;

			Vector3 v0 = { cos(lat0) * cos(lon0), sin(lat0), cos(lat0) * sin(lon0) };
			Vector3 v1 = { cos(lat1) * cos(lon0), sin(lat1), cos(lat1) * sin(lon0) };
			Vector3 v2 = { cos(lat0) * cos(lon1), sin(lat0), cos(lat0) * sin(lon1) };
			Vector3 v3 = { cos(lat1) * cos(lon1), sin(lat1), cos(lat1) * sin(lon1) };

			vertices[start + 0].position = { v0.x, v0.y, v0.z, 1.0f };
			vertices[start + 1].position = { v1.x, v1.y, v1.z, 1.0f };
			vertices[start + 2].position = { v2.x, v2.y, v2.z, 1.0f };

			vertices[start + 3].position = { v2.x, v2.y, v2.z, 1.0f };
			vertices[start + 4].position = { v1.x, v1.y, v1.z, 1.0f };
			vertices[start + 5].position = { v3.x, v3.y, v3.z, 1.0f };

			// 半径1の球なので、頂点位置がそのまま法線になる
			vertices[start + 0].normal = v0;
			vertices[start + 1].normal = v1;
			vertices[start + 2].normal = v2;

			vertices[start + 3].normal = v2;
			vertices[start + 4].normal = v1;
			vertices[start + 5].normal = v3;

			float u0 = float(lonIndex) / subdivision;
			float u1 = float(lonIndex + 1) / subdivision;
			float v0_uv = 1.0f - float(latIndex) / subdivision;
			float v1_uv = 1.0f - float(latIndex + 1) / subdivision;

			vertices[start + 0].texcoord = { u0, v0_uv };
			vertices[start + 1].texcoord = { u0, v1_uv };
			vertices[start + 2].texcoord = { u1, v0_uv };

			vertices[start + 3].texcoord = { u1, v0_uv };
			vertices[start + 4].texcoord = { u0, v1_uv };
			vertices[start + 5].texcoord = { u1, v1_uv };
		}
	}

	return vertices;
}
