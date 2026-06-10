#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream> // 時間を扱うライブラリ
#include <filesystem> // ファイルに書いたり読んだりするライブラリ
#include <chrono>
#include <format>
#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include "ConvertString.h"
#include "log.h"
#include "CrashHandle.h"
#include "Matrix4x4.h"
#include "TransformData3D.h"
#include "VertexData.h"
#include <cmath>
#include "Material.h"
#include "TransformationMatrix.h"
#include "Engine/Light/DirectionalLight.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM LParam);
#endif





#include "Engine/Core/WinApp.h"
#include "Engine/Core/DirectXCore.h"
#include "Engine/Graphics/ShaderCompiler.h"
#include "Engine/Geometry/GeometryGenerator.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/DescriptorHeapManager.h"
#include "Engine/Graphics/PipelineManager.h"

#include "LoadObjFile.h"
#include "functions.h"




// クライアント領域のサイズ
constexpr int32_t kClientWidth = 1280;
constexpr int32_t kClientHeight = 720;

#pragma region 関数群

#pragma endregion

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	// COMの初期化
	HRESULT hr = S_OK;
	hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	// 誰も補足しなかった場合に(Unhandled)、補足する関数を登録
	SetUnhandledExceptionFilter(ExportDump);

#pragma region logs

	// ログのディレクトリを用意
	std::filesystem::create_directory("logs");

	// 現在時刻を取得（UTF時刻）
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	// ログファイルの名前にコンマ何秒はいらないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	// 日本時間（PCの設定時間）に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };
	// formatを使って年月日_時分秒の文字列に変換
	std::string dataString = std::format("{:%Y%m%d_%H%M%S}", localTime);
	// 時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dataString + ".log";
	// ファイルを作って書き込み準備
	std::ofstream logStream(logFilePath);

	logStream.flush();

#pragma endregion

#pragma region ウィンドウ生成・表示

	WinApp* winApp = WinApp::GetInstance();
	winApp->Initialize();

	MSG msg{};

#pragma endregion

	// DirectX12の初期化
	DirectXCore::GetInstance()->Initialize(
		winApp->GetHwnd(),
		kClientWidth,
		kClientHeight
	);

	// CompileShader
	ShaderCompiler::GetInstance()->Initialize();

	// CommandListを取得
	ID3D12GraphicsCommandList* commandList =
		DirectXCore::GetInstance()->GetCommandList();


	ID3D12Device* device =
		DirectXCore::GetInstance()->GetDevice();

	ID3D12DescriptorHeap* srvDescriptorHeap =
		DirectXCore::GetInstance()->GetSRVDescriptorHeap();

	ID3D12DescriptorHeap* dsvDescriptorHeap =
		DirectXCore::GetInstance()->GetDSVDescriptorHeap();

	D3D12_VIEWPORT viewport =
		DirectXCore::GetInstance()->GetViewport();

	D3D12_RECT scissorRect =
		DirectXCore::GetInstance()->GetScissorRect();

#pragma region DebugLayer

	// 初期化完了のログをだす
	Log("Complete create D3D12Device!!!\n");
#ifdef _DEBUG
	ID3D12InfoQueue* infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// ヤバイエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		// 警告時に止まる
		//infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			// Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
			// https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};
		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		// 指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);
		// 開放
		infoQueue->Release();
	}
#endif

#pragma endregion

#pragma region DepthStencilResourceを作る

	ID3D12Resource* depthStencilResource =
		CreateDepthStencilTextureResource(
		device,
		kClientWidth,
		kClientHeight
		);

	// DSV設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	// DSV生成
	device->CreateDepthStencilView(
		depthStencilResource,
		&dsvDesc,
		dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart()
	);

#pragma endregion

#pragma region RootSignatureを生成する

	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// RootParameter作成。PixelShaderのMaterialとVertexShaderのTransform
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0; // 0から始まる
	descriptorRange[0].NumDescriptors = 1; // 数は1つ
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; //CBVを使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; //PixelShaderで使う
	rootParameters[0].Descriptor.ShaderRegister = 0; // レジスタ番号0を使う
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; //CBVを使う

	rootParameters[2].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_CBV;

	rootParameters[2].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;

	rootParameters[2].Descriptor.ShaderRegister = 1;

	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; //VertexShaderで使う
	rootParameters[1].Descriptor.ShaderRegister = 0; // レジスタ番号0を使う
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[3].DescriptorTable.pDescriptorRanges = descriptorRange; // Tableの中身の配列を指定
	rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数
	descriptionRootSignature.pParameters = rootParameters; // ルートパラメータ配列へのポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters); // 配列の長さ

	// Samplerの設定
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0～1の範囲外をリピート
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; // 比較しない
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX; //ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0; //レジスタ番号0を使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	//シリアライズしてバイナリにする
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	// バイナリを元に生成
	ID3D12RootSignature* rootSignature = nullptr;
	hr = device->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

#pragma endregion

#pragma region InputLayoutの設定を行う

	// InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset =
		D3D12_APPEND_ALIGNED_ELEMENT;
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

#pragma endregion

#pragma region BlendStateの設定を行う

	// BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};
	//すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

#pragma endregion

#pragma region RasterizerStateの設定を行う

	// RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	// 裏面(時計回り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

#pragma endregion

#pragma region DepthStencilStateの設定
	//DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	//Depthの機能を有効化する
	depthStencilDesc.DepthEnable = true;
	//書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	//比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
#pragma endregion

#pragma region ShaderをCompileする

	//Shaderをコンパイルする
	IDxcBlob* vertexShaderBlob =
		ShaderCompiler::GetInstance()->Compile(
		L"Shaders/Object3D.VS.hlsl",
		L"vs_6_0"
		);
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob =
		ShaderCompiler::GetInstance()->Compile(
		L"Shaders/Object3D.PS.hlsl",
		L"ps_6_0"
		);
	assert(pixelShaderBlob != nullptr);

#pragma endregion

#pragma region PSOを生成する
	PipelineManager::PipelineConfig pipelineConfig{};
	// device
	pipelineConfig.device = device;
	// root signature
	pipelineConfig.rootSignature = rootSignature;
	// input layout
	pipelineConfig.inputLayout = inputLayoutDesc;
	// blend / rasterizer / depth
	pipelineConfig.blendDesc = blendDesc;
	pipelineConfig.rasterizerDesc = rasterizerDesc;
	pipelineConfig.depthStencilDesc = depthStencilDesc;
	// formats
	pipelineConfig.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	pipelineConfig.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	// shaders
	pipelineConfig.vertexShader = vertexShaderBlob;
	pipelineConfig.pixelShader = pixelShaderBlob;
	// PSO生成
	ID3D12PipelineState* graphicsPipelineState =
		PipelineManager::CreateGraphicsPipeline(pipelineConfig);
#pragma endregion

	// テクスチャデータの数
	std::vector<TextureData> textures(2);
	// 読み込むファイル一覧
	std::vector<std::string> texturePaths = {
		"resources/uvChecker.png",
		"resources/monsterBall.png"
	};
	uint32_t triangleTextureIndex = 0;
	uint32_t sphereTextureIndex = 1;
	uint32_t ObjTextureIndex = 0;
	uint32_t spriteTextureIndex = 0;
	// SRVハンドル
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> textureSrvHandleGPU(textures.size());

	// Textureを読んで転送する
	for (size_t i = 0; i < textures.size(); i++) {
		textures[i] =
			LoadTextureAndUpload(
			texturePaths[i],
			device,
			commandList
			);
	}

#pragma region VertexResourceを生成する

#pragma region 3D用VertexResourceを生成
	//三角形用頂点リソースを作る
	ID3D12Resource* vertexResourceTriangle = CreateBufferResource(device, sizeof(VertexData) * 6);

	// モデル読み込み
	ModelData modelData = LoadObjFile("resources", "axis.obj");
	TextureData objTexture = TextureManager::LoadTexture(modelData.material.textureFilePath);
	// 頂点リソースを作る
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * modelData.vertices.size());
	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewObj{};
	vertexBufferViewObj.BufferLocation = vertexResource->GetGPUVirtualAddress(); // リソースの先頭のアドレスから使う
	vertexBufferViewObj.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size()); // 使用するリソースのサイズは頂点のサイズ
	vertexBufferViewObj.StrideInBytes = sizeof(VertexData); // 1頂点あたりのサイズ

	//顶点リソースにデータを書き込む
	VertexData* vertexData = nullptr;
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData)); // 書き込むためのアドレスを取得
	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size()); // 頂点データをリソースにコピー

	MaterialResource material =
		CreateMaterialResource(device, true);

	uint32_t subdivision = 16;
	uint32_t prevSubdivision = subdivision;
	// 球の頂点リソースを作る
	uint32_t sphereVertexCount = subdivision * subdivision * 6;
	ID3D12Resource* vertexResourceSphere = CreateBufferResource(device, sizeof(VertexData) * sphereVertexCount);

#pragma endregion

	// 今後、オブジェクトごとにマテリアルを持たせる設計のほうがよい

#pragma region 2D用VertexResourceを生成

	//Sprite用頂点リソースを作る
	ID3D12Resource* vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 4);

#pragma region Indexを作る
	ID3D12Resource* indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	// リソースの先頭のアドレスから使う
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	//使用するリソースのサイズはインデックス6つ分のサイズ 
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	//インデックスはuint32_tとする 
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;
#pragma endregion

	//Sprite用マテリアルリソースを作る
	ID3D12Resource* materialResourceSprite = CreateBufferResource(device, sizeof(Material));
	Material* materialDataSprite = nullptr;

	materialResourceSprite->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&materialDataSprite)
	);

	materialDataSprite->color = { 1.0f,1.0f,1.0f,1.0f };

	// 2Dスプライトでは無効にする
	materialDataSprite->enableLighting = false;

	materialDataSprite->uvTransform = MakeIdentity4x4();

#pragma endregion

#pragma endregion

#pragma region VertexBufferViewを作成する

#pragma region 3D用VertexBufferViewを作成

	//三角形の頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewTriangle{};
	// リソースの先頭のアドレスから使う
	vertexBufferViewTriangle.BufferLocation = vertexResourceTriangle->GetGPUVirtualAddress();
	//使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferViewTriangle.SizeInBytes = sizeof(VertexData) * 6;
	// 1頂点あたりのサイズ
	vertexBufferViewTriangle.StrideInBytes = sizeof(VertexData);

	//球の頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSphere{};



#pragma endregion

#pragma region 2D用VertexBufferViewを作成

	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	// リソースの先頭のアドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	//使用するリソースのサイズは頂点分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;
	//1頂点あたりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

#pragma endregion

#pragma endregion

#pragma region Resourceにデータを書き込む

#pragma region 三角形の頂点リソースにデータを書き込む

	//頂点リソースにデータを書き込む
	VertexData* vertexDataTriangle = nullptr;
	//書き込むためのアドレスを取得
	vertexResourceTriangle->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataTriangle));

	// 左下
	vertexDataTriangle[0].position = { -0.5f, -0.5f, 0.0f, 1.0f };
	vertexDataTriangle[0].texcoord = { 0.0f, 1.0f };
	// 上
	vertexDataTriangle[1].position = { 0.0f, 0.5f, 0.0f, 1.0f };
	vertexDataTriangle[1].texcoord = { 0.5f, 0.0f };
	// 右下
	vertexDataTriangle[2].position = { 0.5f, -0.5f, 0.0f, 1.0f };
	vertexDataTriangle[2].texcoord = { 1.0f, 1.0f };

	// 1枚目の三角形を貫通する三角形
	// 左下
	vertexDataTriangle[3].position = { -0.5f, -0.5f, 0.5f, 1.0f };
	vertexDataTriangle[3].texcoord = { 0.0f, 1.0f };
	// 上
	vertexDataTriangle[4].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertexDataTriangle[4].texcoord = { 0.5f, 0.0f };
	// 右下
	vertexDataTriangle[5].position = { 0.5f, -0.5f, -0.5f, 1.0f };
	vertexDataTriangle[5].texcoord = { 1.0f, 1.0f };

	// normal
	vertexDataTriangle[0].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataTriangle[1].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataTriangle[2].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataTriangle[3].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataTriangle[4].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataTriangle[5].normal = { 0.0f, 0.0f, -1.0f };
#pragma endregion

#pragma region 球の頂点リソースにデータを書き込む

	GenerateSphere(subdivision, vertexResourceSphere, vertexBufferViewSphere, sphereVertexCount);

#pragma endregion

#pragma region TransformationMatrix用のResourceを作る

	TransformResource triangleTransform =
		CreateTransformResource(device);

	TransformResource sphereTransform =
		CreateTransformResource(device);

	TransformResource objTransform =
		CreateTransformResource(device);

	TransformResource spriteTransform =
		CreateTransformResource(device);

#pragma endregion

#pragma region 頂点データを設定する(2D)

#pragma region Index

	//インデックスリソースにデータを書き込む
	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0;
	indexDataSprite[1] = 1;
	indexDataSprite[2] = 2;
	indexDataSprite[3] = 1;
	indexDataSprite[4] = 3;
	indexDataSprite[5] = 2;

#pragma endregion

	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	// position
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f };// 左下
	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };// 左上
	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f };// 右下
	vertexDataSprite[3].position = { 640.0f, 0.0f, 0.0f, 1.0f }; // 右上

	// texcoord
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };	
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[3].texcoord = { 1.0f,0.0f };

	// normal
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };
	vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };
#pragma endregion

	DirectionalLightResource directionalLight =
		CreateDirectionalLight(device);

	directionalLight.data->color =
	{
		1.0f,
		1.0f,
		1.0f,
		1.0f
	};

	directionalLight.data->direction =
	{
		0.0f,
		-1.0f,
		0.0f
	};

	directionalLight.data->intensity = 1.0f;


#pragma endregion

	// Transform変数を作る
	Transform3D transformTriangle{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {2.5f, 0.0f, 0.0f} };
	Transform3D transformSphere{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	Transform3D transformObj{ {1.0f, 1.0f, 1.0f}, {0.0f, 3.1415f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	Transform3D transformSprite { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

	Transform3D uvTransformSprite{
	{1.0f,1.0f,1.0f},
	{0.0f,0.0f,0.0f},
	{0.0f,0.0f,0.0f}
	};

	Transform3D cameraTransform { { 1.0f, 1.0f, 1.0f }, { 0.04f, 0.0f, 0.0f }, { 0.0f, 1.7f, -10.0f } };

#pragma region ImGuiの初期化
#ifdef USE_IMGUI
	// ImGuiの初期化
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(winApp->GetHwnd());
	ImGui_ImplDX12_Init(
		device,
		DirectXCore::GetSwapChainBufferCount(),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		srvDescriptorHeap,
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
	);
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif
#pragma endregion

#pragma region ShaderResourceViewを作る
	// metaDataを基にSRVの設定
	for (size_t i = 0; i < textures.size(); ++i) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = textures[i].metadata.format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // 2Dテクスチャ
		srvDesc.Texture2D.MipLevels = UINT(textures[i].metadata.mipLevels);

		//SRVを作成する
		uint32_t srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		// index 1 を使う（0 は ImGui）
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = DescriptorHeapManager::GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, static_cast<int>(i) + 1);
		textureSrvHandleGPU[i] = DescriptorHeapManager::GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, static_cast<int>(i) + 1);

		//SRVの生成
		device->CreateShaderResourceView(textures[i].textureResource, &srvDesc, textureSrvHandleCPU);
	}
#pragma endregion

	// ウィンドウのxボタンが押されるまでループ
	while (winApp->ProcessMessage()) {

#pragma region ImGui
#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			// 開発用UIの処理
			ImGui::Begin("3D Objects");

			// ----Triangle----
			if (ImGui::TreeNode("Triangle")) {
				ImGui::PushID("Triangle");

				ImGui::DragFloat3("scale", &transformTriangle.scale.x, 0.01f);
				ImGui::DragFloat3("rotate", &transformTriangle.rotate.x, 0.01f);
				ImGui::DragFloat3("translate", &transformTriangle.translate.x, 0.01f);
				ImGui::Separator();
				ImGui::DragFloat4("Vertex0", &vertexDataTriangle[0].position.x, 0.01f);
				ImGui::DragFloat4("Vertex1", &vertexDataTriangle[1].position.x, 0.01f);
				ImGui::DragFloat4("Vertex2", &vertexDataTriangle[2].position.x, 0.01f);
				ImGui::Separator();

				ImGui::ColorEdit4("Color", &material.data->color.x);
				bool lighting =
					material.data->enableLighting != 0;

				if (ImGui::Checkbox("Enable Lighting", &lighting)) {
					material.data->enableLighting = lighting;
				}

				const char* textureItems[] = {
					"uvChecker",
					"monsterBall"
				};
				ImGui::Combo("Texture", reinterpret_cast<int*>(&triangleTextureIndex), textureItems, IM_ARRAYSIZE(textureItems));

				ImGui::PopID();
				ImGui::TreePop();
			}

			// ----Sphere----
			if (ImGui::TreeNode("Sphere")) {
				ImGui::PushID("Sphere");

				ImGui::DragFloat3("scale", &transformSphere.scale.x, 0.01f);
				ImGui::DragFloat3("rotate", &transformSphere.rotate.x, 0.01f);
				ImGui::DragFloat3("translate", &transformSphere.translate.x, 0.01f);
				ImGui::Separator();

				ImGui::DragInt("Sphere Subdivision", (int*)&subdivision, 1, 3, 128);

				if (subdivision != prevSubdivision) {

					// FenceでGPU完了待ちをする
					DirectXCore::GetInstance()->WaitForGPU();
					// 完了が確認できたらRelease
					vertexResourceSphere->Release();

					sphereVertexCount = subdivision * subdivision * 6;

					vertexResourceSphere = CreateBufferResource(device, sizeof(VertexData) * sphereVertexCount);

					GenerateSphere(subdivision, vertexResourceSphere, vertexBufferViewSphere, sphereVertexCount);

					prevSubdivision = subdivision;
				}
				ImGui::Separator();

				const char* textureItems[] = {
					"uvChecker",
					"monsterBall"
				};

				ImGui::Combo("Texture", reinterpret_cast<int*>(&sphereTextureIndex), textureItems, IM_ARRAYSIZE(textureItems));

				ImGui::PopID();
				ImGui::TreePop();
			}

			// ----Obj----
			if (ImGui::TreeNode("Obj")) {
				ImGui::PushID("Obj");

				ImGui::DragFloat3("scale", &transformObj.scale.x, 0.01f);
				ImGui::DragFloat3("rotate", &transformObj.rotate.x, 0.01f);
				ImGui::DragFloat3("translate", &transformObj.translate.x, 0.01f);
				ImGui::Separator();

				ImGui::ColorEdit4("Color", &material.data->color.x);
				bool lighting =
					material.data->enableLighting != 0;

				if (ImGui::Checkbox("Enable Lighting", &lighting)) {
					material.data->enableLighting = lighting;
				}

				const char* textureItems[] = {
					"uvChecker",
					"monsterBall"
				};
				ImGui::Combo("Texture", reinterpret_cast<int*>(&ObjTextureIndex), textureItems, IM_ARRAYSIZE(textureItems));

				ImGui::PopID();
				ImGui::TreePop();
			}

			ImGui::End();

			ImGui::Begin("2D Objects");
			if (ImGui::TreeNode("Square")) {
				ImGui::PushID("Square");
			
				ImGui::DragFloat3("scale", &transformSprite.scale.x, 0.01f);
				ImGui::DragFloat3("rotate", &transformSprite.rotate.x, 0.05f);
				ImGui::DragFloat3("translate", &transformSprite.translate.x, 0.35f);
				ImGui::Separator();
				ImGui::DragFloat4("Vertex0 position", &vertexDataSprite[0].position.x, 0.2f);
				ImGui::DragFloat4("Vertex1 position", &vertexDataSprite[1].position.x, 0.2f);
				ImGui::DragFloat4("Vertex2 position", &vertexDataSprite[2].position.x, 0.2f);
				ImGui::DragFloat4("Vertex3 position", &vertexDataSprite[3].position.x, 0.2f);

				ImGui::DragFloat2("Vertex0 texcoord", &vertexDataSprite[0].texcoord.x, 0.2f);
				ImGui::DragFloat2("Vertex1 texcoord", &vertexDataSprite[1].texcoord.x, 0.2f);
				ImGui::DragFloat2("Vertex2 texcoord", &vertexDataSprite[2].texcoord.x, 0.2f);
				ImGui::DragFloat2("Vertex3 texcoord", &vertexDataSprite[3].texcoord.x, 0.2f);
			
				ImGui::Separator();

				ImGui::DragFloat2(
					"UVTranslate",
					&uvTransformSprite.translate.x,
					0.01f,
					-10.0f,
					10.0f
				);

				ImGui::DragFloat2(
					"UVScale",
					&uvTransformSprite.scale.x,
					0.01f,
					-10.0f,
					10.0f
				);

				ImGui::SliderAngle(
					"UVRotate",
					&uvTransformSprite.rotate.z
				);

				ImGui::Separator();

				bool lightingSprite =
					materialDataSprite->enableLighting != 0;
				if (ImGui::Checkbox("Enable Lighting", &lightingSprite)) {
					materialDataSprite->enableLighting = lightingSprite;
				}

				const char* textureItems[] = {
					"uvChecker",
					"monsterBall"
				};

				ImGui::Combo("Texture", reinterpret_cast<int*>(&spriteTextureIndex), textureItems, IM_ARRAYSIZE(textureItems));

				ImGui::PopID();
				ImGui::TreePop();
			}
			ImGui::End();

			ImGui::Begin("Camera, DirectionalLight");

			ImGui::DragFloat3("Camera scale", &cameraTransform.scale.x, 0.01f);
			ImGui::DragFloat3("Camera rotate", &cameraTransform.rotate.x, 0.01f);
			ImGui::DragFloat3("Camera translate", &cameraTransform.translate.x, 0.01f);
				
			ImGui::Separator();

			ImGui::ColorEdit4(
				"Light Color",
				&directionalLight.data->color.x
			);

			ImGui::DragFloat3(
				"Light Direction",
				&directionalLight.data->direction.x,
				0.01f
			);

			ImGui::DragFloat(
				"Intensity",
				&directionalLight.data->intensity,
				0.01f,
				0.0f,
				10.0f
			);

			ImGui::End();

			//ImGuiの内部コマンドを生成する
			ImGui::Render();
#endif
#pragma endregion

#pragma region ゲームの処理
			// ゲームの処理
			transformTriangle.rotate.y += 0.04f;
			transformSphere.rotate.y += 0.02f;
#pragma endregion

#pragma region 行列計算

			// 透視投影行列
			Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
			Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = Inverse(cameraMatrix);

			// 三角形
			UpdateTransformMatrix(
				triangleTransform,
				transformTriangle,
				viewMatrix,
				projectionMatrix
			);

			// 球
			UpdateTransformMatrix(
				sphereTransform,
				transformSphere,
				viewMatrix,
				projectionMatrix
			);

			// Obj
			UpdateTransformMatrix(
				objTransform,
				transformObj,
				viewMatrix,
				projectionMatrix
			);

			// Sprite用のworldViewProjectionMatrixを作る
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
			spriteTransform.data->WVP =
				worldViewProjectionMatrixSprite;

			spriteTransform.data->World =
				worldMatrixSprite;

			Matrix4x4 uvTransformMatrix =
				MakeScaleMatrix(
				uvTransformSprite.scale
				);

			uvTransformMatrix =
				Multiply(
				uvTransformMatrix,
				MakeRotateZMatrix(
				uvTransformSprite.rotate.z
				)
				);

			uvTransformMatrix =
				Multiply(
				uvTransformMatrix,
				MakeTranslateMatrix(
				uvTransformSprite.translate
				)
				);

			materialDataSprite->uvTransform =
				uvTransformMatrix;

#pragma endregion

			DirectXCore::GetInstance()->BeginFrame();

#pragma region 画面クリア処理
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
				DirectXCore::GetInstance()->GetCurrentRTVHandle();

			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
				DirectXCore::GetInstance()->GetDSVDescriptorHeap()
				->GetCPUDescriptorHandleForHeapStart();

			commandList->OMSetRenderTargets(
				1,
				&rtvHandle,
				false,
				&dsvHandle
			);

			commandList->ClearDepthStencilView(
				dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
				D3D12_CLEAR_FLAG_DEPTH,
				1.0f,
				0,
				0,
				nullptr
			);
#pragma endregion

#pragma region 描画コマンドを積む

#pragma region 3D描画

#pragma region 共通処理
			commandList->RSSetViewports(1, &viewport); // Viewportを設定
			commandList->RSSetScissorRects(1, &scissorRect); //Scissorを設定
			//RootSignatureを設定。PSOに設定しているけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature);
			commandList->SetPipelineState(graphicsPipelineState); // PSOを設定
			//描画用のDescriptorHeapの設定
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);
			//マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, material.resource->GetGPUVirtualAddress());

			commandList->SetGraphicsRootConstantBufferView(
				2,
				directionalLight.resource->GetGPUVirtualAddress()
			);

			//形状を設定。PSOに設定しているものとはまた別。同じものを設定すると考えておけば良い
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
#pragma endregion

			//
			// ---- 三角形描画 ----
			//
			//SRVのDescriptorTableの先頭を設定
			//commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandleGPU[triangleTextureIndex]);
			//commandList->IASetVertexBuffers(0, 1, &vertexBufferViewTriangle); // VBVを設定
			////wvp用のCBufferの場所を設定
			//commandList->SetGraphicsRootConstantBufferView(
			//	1,
			//	triangleTransform.resource->GetGPUVirtualAddress()
			//);
			//描画！(DrawCall/ドローコール)。3頂点で1つのインスタンス。インスタンスについては今後
			//commandList->DrawInstanced(6, 1, 0, 0);
			
			//
			// ---- 球描画 ----
			//
			//DrawObject(
			//	commandList,
			//	vertexBufferViewSphere,
			//	textureSrvHandleGPU[sphereTextureIndex],
			//	sphereTransform.resource,
			//	sphereVertexCount
			//);
			// ---- OBJ描画 ----
			DrawObject(
				commandList,
				vertexBufferViewObj,
				textureSrvHandleGPU[ObjTextureIndex],
				objTransform.resource,
				UINT(modelData.vertices.size())
			);
#pragma endregion

#pragma region 2D描画
			//
			// ---- スプライト描画 ----
			//
			commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandleGPU[spriteTextureIndex]);
			//Spriteの描画。変更が必要なものだけ変更する
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite); // VBVを設定

			commandList->IASetIndexBuffer(&indexBufferViewSprite); // IBVを設定

			commandList->SetGraphicsRootConstantBufferView(
				0,
				materialResourceSprite->GetGPUVirtualAddress()
			);
			// TransformationMatrixCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(
				1,
				spriteTransform.resource->GetGPUVirtualAddress()
			);
			//描画！(DrawCall/ドローコール)6個のインデックスを使用し1つのインスタンスを描画。その他は当面0で良い 
			//commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
#pragma endregion

#pragma endregion

#ifdef USE_IMGUI
			//実際のcommandListのImGuiの描画コマンドを積む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

			DirectXCore::GetInstance()->EndFrame();

	}

#ifdef USE_IMGUI
	// ImGuiの終了処理
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

#pragma region 開放処理
	for (auto& texture : textures) {
		if (texture.textureResource) {
			texture.textureResource->Release();
		}
		if (texture.intermediateResource) {
			texture.intermediateResource->Release();
		}
	}
	ShaderCompiler::GetInstance()->Finalize();
	depthStencilResource->Release();
	vertexResourceSprite->Release();
	vertexResourceSphere->Release();
	vertexResourceTriangle->Release();
	graphicsPipelineState->Release();
	signatureBlob->Release();
	if (errorBlob) {
		errorBlob->Release();
	}
	rootSignature->Release();
	pixelShaderBlob->Release();
	vertexShaderBlob->Release();
	triangleTransform.resource->Release();
	sphereTransform.resource->Release();
	objTransform.resource->Release();
	spriteTransform.resource->Release();
	materialResourceSprite->Release();
	material.resource->Release();

#pragma endregion

	CloseWindow(winApp->GetHwnd());

#pragma region リソースリークチェック
	// リソースリークチェック
	IDXGIDebug1* debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}
#pragma endregion

	// COMの終了処理
	CoUninitialize();
	return 0;
}