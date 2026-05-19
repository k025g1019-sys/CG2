#pragma once
#include <vector>
#include <d3d12.h>
#include "Vector3.h"

struct VertexData;

#pragma region Sphere

/// <summary>
/// 球
/// </summary>
class Sphere {
public:
	Sphere() = default;
	//Sphere();

	void Initialize(ID3D12Device* device, int subdivision);
	void Update();
	void Draw(ID3D12GraphicsCommandList* commandList);

#ifdef _DEBUG
	void DrawImGui();
#endif

	const Vector3 GetCenter() const { return center_; }
	const float GetRadius() const { return radius_; }

	void SetCenter(Vector3 p) { center_ = p; }
	void SetRadius(float p) { radius_ = p; }
	void SetColor(unsigned int p) { color_ = p; }

private:
	void GenerateVertices(int subdivision);

	Vector3 center_; //!< 中心点
	float radius_ = 1.0f;   //!< 半径
	unsigned int color_ = 0xffffffff;

	int subdivision_ = 16;

	std::vector<VertexData> vertices_;

	ID3D12Resource* vertexResource_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vbv_{};
};

#pragma endregion
