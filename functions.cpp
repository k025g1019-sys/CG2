#include <cassert>
#include <cstdint>
#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "Matrix4x4.h"
#include "TransformData3D.h"
#include "TransformationMatrix.h"
#include "Material.h"
#include "Engine/Light/DirectionalLight.h"
#include "Engine/Graphics/TextureManager.h"
#include "functions.h"

#pragma region Resource作成の関数
// Resource作成の関数
ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {

	//頂点リソース用のヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD; // UploadHeap
	// 頂点リソースの設定
	D3D12_RESOURCE_DESC vertexResourceDesc{};
	//バッファリソース。テクスチャの場合はまた別の設定をする
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexResourceDesc.Width = sizeInBytes;//リソースのサイズ
	//バッファの場合はこれらは1にする決まり
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;
	// バッファの場合はこれにする決まり
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	//実際に頂点リソースを作る
	ID3D12Resource* vertexResource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&vertexResource));
	assert(SUCCEEDED(hr));
	return vertexResource;
};
#pragma endregion

#pragma region DepthStencilTextureを作る
ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height) {
	// 生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width; // Textureの幅
	resourceDesc.Height = height; // Textureの高さ
	resourceDesc.MipLevels = 1; // mipmapの数
	resourceDesc.DepthOrArraySize = 1; // 奥行き or 配列Textureの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // DepthStencilとして利用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1; // サンプリングカウント。1固定。
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知

	//利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

	//深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f; // 1.0f（最大数）でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。Resourceと合わせる

	//Resourceの生成
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, // Heapの設定
		D3D12_HEAP_FLAG_NONE,//Heapの特殊な設定。特になし。
		&resourceDesc, // Resourceの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE, //深度値を書き込む状態にしておく
		&depthClearValue, // Clear最適値
		IID_PPV_ARGS(&resource)); //作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));
	return resource;
};
#pragma endregion

#pragma region TransformationMatrix生成

TransformResource CreateTransformResource(
	ID3D12Device* device) {
	TransformResource result;

	result.resource =
		CreateBufferResource(
		device,
		sizeof(TransformationMatrix));

	result.resource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&result.data));

	result.data->WVP = MakeIdentity4x4();
	result.data->World = MakeIdentity4x4();

	return result;
}

#pragma endregion

#pragma region Material生成

MaterialResource CreateMaterialResource(
	ID3D12Device* device,
	bool enableLighting) {
	MaterialResource result;

	result.resource =
		CreateBufferResource(
		device,
		sizeof(Material));

	result.resource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&result.data));

	result.data->color =
	{
		1.0f,
		1.0f,
		1.0f,
		1.0f
	};

	result.data->enableLighting =
		enableLighting;

	result.data->uvTransform =
		MakeIdentity4x4();

	return result;
}

#pragma endregion 

#pragma region DirectionalLight生成

DirectionalLightResource CreateDirectionalLight(
	ID3D12Device* device) {
	DirectionalLightResource result;

	result.resource =
		CreateBufferResource(
		device,
		sizeof(DirectionalLight));

	result.resource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&result.data));

	result.data->color =
	{
		1,1,1,1
	};

	result.data->direction =
	{
		0,-1,0
	};

	result.data->intensity = 1.0f;

	return result;
}

#pragma endregion

#pragma region Textureロード

TextureData LoadTextureAndUpload(
	const std::string& path,
	ID3D12Device* device,
	ID3D12GraphicsCommandList* commandList) {
	TextureData texture =
		TextureManager::LoadTexture(path);

	texture.textureResource =
		TextureManager::CreateTextureResource(
		device,
		texture.metadata);

	texture.intermediateResource =
		TextureManager::UploadTextureData(
		texture.textureResource,
		texture.mipImage,
		device,
		commandList);

	return texture;
}

#pragma endregion

#pragma region 行列更新

void UpdateTransformMatrix(
	TransformResource& transformResource,
	const Transform3D& transform,
	const Matrix4x4& view,
	const Matrix4x4& projection) {
	Matrix4x4 world =
		MakeAffineMatrix(
		transform.scale,
		transform.rotate,
		transform.translate);

	Matrix4x4 wvp =
		Multiply(
		world,
		Multiply(
		view,
		projection));

	transformResource.data->World = world;
	transformResource.data->WVP = wvp;
}

#pragma endregion

#pragma region DrawObject関数

void DrawObject(
	ID3D12GraphicsCommandList* commandList,
	D3D12_VERTEX_BUFFER_VIEW& vbv,
	D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
	ID3D12Resource* transformResource,
	uint32_t vertexCount) {
	commandList->SetGraphicsRootDescriptorTable(
		3,
		textureHandle);

	commandList->IASetVertexBuffers(
		0,
		1,
		&vbv);

	commandList->SetGraphicsRootConstantBufferView(
		1,
		transformResource->GetGPUVirtualAddress());

	commandList->DrawInstanced(
		vertexCount,
		1,
		0,
		0);
}

#pragma endregion
