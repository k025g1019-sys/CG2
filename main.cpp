#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream> // 時間を扱うライブラリ
#include <filesystem> // ファイルに書いたり読んだりするライブラリ
#include <chrono>
#include <format>
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
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

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM LParam);
#endif

// クライアント領域のサイズ
constexpr int32_t kClientWidth = 1280;
constexpr int32_t kClientHeight = 720;

#pragma region 関数群

#pragma region ウィンドウプロシージャ
// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}
#endif

	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
		// ウィンドウが破棄された
	case WM_DESTROY:
	// OSに対して、アプリの終了を伝える
	PostQuitMessage(0);
	return 0;
	}

	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}
#pragma endregion

#pragma region CompileShader関数
IDxcBlob* CompileShader(
	// CompilerするShaderファイルへのパス
	const std::wstring& filePath,
	// Compilerに使用するProfile
	const wchar_t* profile,
	// 初期化で生成したものを3つ
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler) {
	// 中身を書いていく
	// 1.hlslファイルを読む
	// これからシェーダーをコンパイルする旨をログに出す
	Log(ConvertString(std::format(L"Begin CompileShader, path: {}, profile: {}\n", filePath, profile)));
	// hlslファイルを読む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	// 読めなかったら止める
	assert(SUCCEEDED(hr));
	// 読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8; //UTF8の文字コードであることを通知

	//2. Compileする
	LPCWSTR arguments[] = {
		filePath.c_str(), //コンパイル対象のhlslファイル名
		L"-E", L"main", //エントリーポイントの指定。基本的にmain以外にはしない
		L"-T", profile, //ShaderProfileの設定
		L"-Zi", L"-Qembed_debug", //デバッグ用の情報を埋め込む
		L"-Od", //最適化を外しておく
		L"-Zpr", // メモリレイアウトは行優先
	};
	//実際にShaderをコンパイルする
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer, // 読み込んだファイル
		arguments, // コンパイルオプション
		_countof(arguments), // コンパイルオプションの数
		includeHandler, // includeが含まれた諸々
		IID_PPV_ARGS(&shaderResult) //コンパイル結果
	);
	// コンパイルエラーではなくdxcが起動できないなど致命的な状況
	assert(SUCCEEDED(hr));

	// 3.警告·エラーがでていないか確認する
	// 警告·エラーが出てたらログに出して止める
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());
		//警告·エラーダメゼッタイ
		assert(false);
	}

	//4.Compile結果を受け取って返す
	//コンパイル結果から実行用のバイナリ部分を取得
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	//成功したログを出す
	Log(ConvertString(std::format(L"Compile Succeeded, path: {}, profile: {}\n", filePath, profile)));
	//もう使わないリソースを解放
	shaderSource->Release();
	shaderResult->Release();
	//実行用のバイナリを返却
	return shaderBlob;
}
#pragma endregion

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

#pragma region DescriptorHeapの作成関数
ID3D12DescriptorHeap* CreateDescriptorHeap(
	ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
	ID3D12DescriptorHeap* descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}
#pragma endregion

// テクスチャデータ
struct TextureData {
	DirectX::ScratchImage mipImage;
	DirectX::TexMetadata metadata;
	ID3D12Resource* textureResource = nullptr;
	ID3D12Resource* intermediateResource = nullptr;
};

#pragma region テクスチャデータを読む
DirectX::ScratchImage LoadTexture(const std::string& filepath)
{
//テクスチャファイルを読んでプログラムで扱えるようにする
DirectX::ScratchImage image{};
std::wstring filePathw = ConvertString(filepath);
HRESULT hr = DirectX::LoadFromWICFile(filePathw.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
assert(SUCCEEDED(hr));

// ミップマップの作成
DirectX::ScratchImage mipImages{};
hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
assert(SUCCEEDED(hr));

// ミップマップ付きのデータを返す
return mipImages;
}
#pragma endregion

// マテリアルデータ
struct Material {
	Vector4 color;
	int32_t enableLighting;
	//float padding[3];
};

struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 World;
};

// 平行光源
struct DirectionalLight {
	Vector4 color; //!< ライトの色
	Vector3 direction; //!< ライトの色
	float intensity; //!< 輝度
};

#pragma region DirectX12のTextureResourceを作る
ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata)
{
	// 1. metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width); // Textureの幅
	resourceDesc.Height = UINT(metadata.height); // Textureの高さ
	resourceDesc.MipLevels = UINT16(metadata.mipLevels); // mipmapの数
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize); // 奥行 or 配列Textureの配列数
	resourceDesc.Format = metadata.format; // TextureのFormat
	resourceDesc.SampleDesc.Count = 1; // サンプリングカウント。1固定。
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension); // Textureの次元数。普段使っているのは2次元
	// 2. 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	// 3. Resourceを生成する
	//Resourceの生成
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, // Heapの設定
		D3D12_HEAP_FLAG_NONE,//Heapの特殊な設定。特になし。
		&resourceDesc, // Resourceの設定
		D3D12_RESOURCE_STATE_COPY_DEST, // データ転送される設定
		nullptr, // Clear最適値。使わないのでnullptr
		IID_PPV_ARGS(&resource)); // 作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));
	return resource;
}
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

#pragma region TextureResourceにデータを転送する
[[ nodiscard]]
ID3D12Resource* UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12Device* device,ID3D12GraphicsCommandList* commandList)
{
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));
	ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);
	UpdateSubresources(commandList, texture, intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());
	// Textureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENERIC_READへResourceStateを変更する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);
	return intermediateResource;
}
#pragma endregion

#pragma region 球を生成する
void GenerateSphere(
	uint32_t subdivision,
	ID3D12Resource* vertexResourceSphere,
	D3D12_VERTEX_BUFFER_VIEW& vertexBufferViewSphere,
	uint32_t sphereVertexCount) {
	const float pi = 3.1415926535f;

	sphereVertexCount = subdivision * subdivision * 6;

	std::vector<VertexData> vertices;
	vertices.resize(sphereVertexCount);

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

			float u0 = (float)lonIndex / subdivision;
			float u1 = (float)(lonIndex + 1) / subdivision;
			float v0_uv = 1.0f - (float)latIndex / subdivision;
			float v1_uv = 1.0f - (float)(latIndex + 1) / subdivision;

			vertices[start + 0].texcoord = { u0, v0_uv };
			vertices[start + 1].texcoord = { u0, v1_uv };
			vertices[start + 2].texcoord = { u1, v0_uv };

			vertices[start + 3].texcoord = { u1, v0_uv };
			vertices[start + 4].texcoord = { u0, v1_uv };
			vertices[start + 5].texcoord = { u1, v1_uv };

			// 法線
			for (int i = 0; i < 6; i++) {
				Vector3 p = {
					vertices[start + i].position.x,
					vertices[start + i].position.y,
					vertices[start + i].position.z
				};

				vertices[start + i].normal = Normalize(p);
			}
		}
	}

	VertexData* vertexData = nullptr;
	vertexBufferViewSphere.BufferLocation = vertexResourceSphere->GetGPUVirtualAddress();
	vertexBufferViewSphere.SizeInBytes = sizeof(VertexData) * sphereVertexCount;
	vertexBufferViewSphere.StrideInBytes = sizeof(VertexData);

	vertexResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, vertices.data(), sizeof(VertexData) * sphereVertexCount);
}
#pragma endregion

#pragma region DescriptorHandleを取得する
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}
#pragma endregion

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

#pragma region DirectX12の初期化

	// DXGIファクトリーの生成
	IDXGIFactory7* dxgiFactory = nullptr;
	// HRESULTはWindows系のエラーコードであり、
	// 関数が成功したかどうかをSUCCEEDEDマクロで判定できる
	hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	// 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どう
	// にもできない場合が多いのでassertにしておく
	assert(SUCCEEDED(hr));

	// アダプタを決定する
	IDXGIAdapter4* useAdapter = nullptr;
	for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i,
		DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
		DXGI_ERROR_NOT_FOUND; ++i) {

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			std::string message = ConvertString(std::format(L"Use Adapter:{}\n", adapterDesc.Description));
			Log(logStream, message);
			break;
		}
		useAdapter->Release();
		useAdapter = nullptr;
	}
	assert(useAdapter != nullptr);

	// D3D12Deviceの生成
	ID3D12Device* device = nullptr;
	// 機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
	D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0
	};

#ifdef _DEBUG
	ID3D12Debug1* debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();
		// さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };
	// 高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		// 採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device));
		// 指定した機能レベルでデバイスが生成できたかを確認
		if (SUCCEEDED(hr)) {
			// 生成できたのでログ出力を行ってループを抜ける
			Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}
	// デバイスの生成がうまくいかなかったので起動できない
	assert(device != nullptr);

#pragma endregion

#pragma region ウィンドウ生成・表示

	WNDCLASS wc{};
	// ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名
	wc.lpszClassName = L"WindowClass";
	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録する
	RegisterClass(&wc);

	// ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };

	// クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウの生成
	HWND hwnd = CreateWindow(
		wc.lpszClassName,	  // 利用するクラス名
		L"Window",			  // タイトルバーの文字
		WS_OVERLAPPEDWINDOW,  // ウィンドウスタイル
		CW_USEDEFAULT,		  // 表示X座標（Windowsに任せる)
		CW_USEDEFAULT,		  // 表示Y座標（WindowsOSに任せる)
		wrc.right - wrc.left, // ウィンドウ横幅
		wrc.bottom - wrc.top, // ウィンドウ縦幅
		nullptr,			  // 親ウィンドウハンドル
		nullptr,			  // メニューハンドル
		wc.hInstance,		  // インスタンスハンドル
		nullptr);			  // オプション

	// ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

	MSG msg{};

#pragma endregion

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

#pragma region Command

	// コマンドキューを生成する
	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	// コマンドキューの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));
	// コマンドアロケータを生成する
	ID3D12CommandAllocator* commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	// コマンドアロケータの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));
	// コマンドリストを生成する
	ID3D12GraphicsCommandList* commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
	// コマンドリストの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));
	// スワップチェーンを生成する
	IDXGISwapChain4* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;   // 画面の幅。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Height = kClientHeight; // 画面の高さ。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 色の形式
	swapChainDesc.SampleDesc.Count = 1; // マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
	swapChainDesc.BufferCount = 2; // ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // モニタにうつしたら、中身を破棄
	// コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));
	assert(SUCCEEDED(hr));

	// ディスクリプタヒープの生成
	ID3D12DescriptorHeap* rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	ID3D12DescriptorHeap* srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// DepthStencilTextureをウィンドウのサイズで作成
	ID3D12Resource* depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);

	// D5V用のヒープでディスクリプタの数は1。DsvはShader内で触るものではないので、ShaderVisibleはfalse
	ID3D12DescriptorHeap* dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // Format。基本的にはResourceに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2dTexture
	// DSVHeapの先頭にDSVをつくる
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// SwapChainからResourceを引っ張ってくる
	ID3D12Resource* swapChainResources[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	// うまく取得できなければ起動できない
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));
	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;      // 出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2sテクスチャとして書き込む

	//DescriptorSizeを取得しておく
	//const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	//const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	// RTVを2つ作るのでディスクリプタを2つ用意
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	// まず1つ目を作る。1つ目は最初のところに作る。作る場所をこちらで指定してあげる必要がある
	rtvHandles[0] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 0);
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	// 2つ目を作る
	rtvHandles[1] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 1);
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

#pragma endregion

#pragma region Fenceを作る
	// 初期値0でFenceを作る
	ID3D12Fence* fence = nullptr;
	uint32_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// Fenceのイベントを待つためのイベントを作成する
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);
#pragma endregion

#pragma region dxcCompilerの初期化

	//dxcCompilerを初期化
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	//現時点でincludeはしないが、includeに対応するための設定を行っておく
	IDxcIncludeHandler* includeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);

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
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; //VertexShaderで使う
	rootParameters[1].Descriptor.ShaderRegister = 0; // レジスタ番号0を使う
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange; // Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderを使う
	rootParameters[3].Descriptor.ShaderRegister = 1; // レジスタ番号1を使う	
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
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
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
	IDxcBlob* vertexShaderBlob = CompileShader(L"Object3D.VS.hlsl",
		L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(L"Object3D.PS.hlsl",
		L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);
	assert(pixelShaderBlob != nullptr);

#pragma endregion

#pragma region PSOを生成する

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature; // RootSignature
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc; // InputLayout
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),
	vertexShaderBlob->GetBufferSize() }; // VertexShader
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),
	pixelShaderBlob->GetBufferSize() }; // PixelShader
	graphicsPipelineStateDesc.BlendState = blendDesc; // BlendState
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc; // RasterizerState
	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	// 利用するトポロジ(形状)のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	// どのように画面に色を打ち込むかの設定(気にしなくて良い)
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	//DepthStencilの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 実際に生成
	ID3D12PipelineState* graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

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
	uint32_t spriteTextureIndex = 0;
	// SRVハンドル
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> textureSrvHandleGPU(textures.size());

	// Textureを読んで転送する
	for (size_t i = 0; i < textures.size(); ++i) {
		// Texture読み込み
		textures[i].mipImage = LoadTexture(texturePaths[i].c_str());
		// Metadata取得
		textures[i].metadata = textures[i].mipImage.GetMetadata();
		// GPU Resource生成
		textures[i].textureResource = CreateTextureResource(device, textures[i].metadata);
		// GPUへ転送
		textures[i].intermediateResource = UploadTextureData(textures[i].textureResource, textures[i].mipImage, device, commandList);
	}

#pragma region VertexResourceを生成する

#pragma region 3D用VertexResourceを生成
	//三角形用頂点リソースを作る
	ID3D12Resource* vertexResourceTriangle = CreateBufferResource(device, sizeof(VertexData) * 6);

	//マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Material));
	// マテリアルにデータを書き込む
	Material* materialData = nullptr;
	//書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	// 白
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	// Lighting有効
	materialData->enableLighting = true;

	uint32_t subdivision = 16;
	uint32_t prevSubdivision = subdivision;
	// 球の頂点リソースを作る
	uint32_t sphereVertexCount = subdivision * subdivision * 6;
	ID3D12Resource* vertexResourceSphere = CreateBufferResource(device, sizeof(VertexData) * sphereVertexCount);

#pragma endregion

#pragma region 2D用VertexResourceを生成
	// Sprite用のマテリアルリソースを作る
	ID3D12Resource* materialResourceSprite = CreateBufferResource(device, sizeof(Material));
	// マテリアルにデータを書き込む
	Material* materialDataSprite = nullptr;
	//書き込むためのアドレスを取得
	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));

	ID3D12Resource* directionalLightResource = CreateBufferResource(device, sizeof(DirectionalLight));
	DirectionalLight* directionalLightData = nullptr;
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
	directionalLightData->intensity = 1.0f;

	// 白
	materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	// SpriteはLightingしないのでfalseを設定する
	materialDataSprite->enableLighting = false;
	// Sprite用頂点リソースを作る
	ID3D12Resource* vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 6);

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
	//使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;
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

	Vector3 n = { 0.0f, 0.0f, -1.0f };

	for (int i = 0; i < 6; i++) {
		vertexDataTriangle[i].normal = n;
	}
#pragma endregion

#pragma region 球の頂点リソースにデータを書き込む

	GenerateSphere(subdivision, vertexResourceSphere, vertexBufferViewSphere, sphereVertexCount);

#pragma endregion

#pragma region TransformationMatrix用のResourceを作る
	//WVP用のリソースを作る
	ID3D12Resource* transformationMatrixResourceTriangle = CreateBufferResource(device, sizeof(TransformationMatrix));
	//データを書き込む
	TransformationMatrix* transformationMatrixDataTriangle = nullptr;
	//書き込むためのアドレスを取得
	hr = transformationMatrixResourceTriangle->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataTriangle));
	assert(SUCCEEDED(hr));
	//単位行列を書きこんでおく
	transformationMatrixDataTriangle->WVP = MakeIdentity4x4();
	transformationMatrixDataTriangle->World = MakeIdentity4x4();

	// 球も同様に行う
	ID3D12Resource* transformationMatrixResourceSphere = CreateBufferResource(device, sizeof(Matrix4x4));
	TransformationMatrix* transformationMatrixDataSphere = nullptr;
	hr = transformationMatrixResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSphere));
	assert(SUCCEEDED(hr));
	transformationMatrixDataSphere->WVP = MakeIdentity4x4();
	transformationMatrixDataSphere->World = MakeIdentity4x4();
#pragma endregion

#pragma region 頂点データを設定する(2D)
	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
	// 1枚目の三角形
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f };// 左下
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };// 左上
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f };// 右下
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	// 2枚目の三角形
	vertexDataSprite[3].position = { 0.0f, 0.0f, 0.0f, 1.0f }; // 左上
	vertexDataSprite[3].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[4].position = { 640.0f, 0.0f, 0.0f, 1.0f };// 右上
	vertexDataSprite[4].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[5].position = { 640.0f, 360.0f, 0.0f, 1.0f };// 右下
	vertexDataSprite[5].texcoord = { 1.0f, 1.0f };

	vertexDataSprite[0].normal = { 0.0f,0.0f,-1.0f };
#pragma endregion

#pragma region TransformationMatrix用のResourceを作る(2D)
	//Sprite用のTransformationMatrix用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(TransformationMatrix));
	//データを書き込む
	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	//書き込むためのアドレスを取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	//単位行列を書きこんでおく
	transformationMatrixDataSprite->WVP = MakeIdentity4x4();
	transformationMatrixDataSprite->World = MakeIdentity4x4();
#pragma endregion

#pragma endregion

#pragma region ViewportとScissor

	// ビューポート
	D3D12_VIEWPORT viewport{};
	//クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// シザー矩形
	D3D12_RECT scissorRect{};
	//基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;

#pragma endregion

	// Transform変数を作る
	Transform3D transformTriangle{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {2.5f, 0.0f, 0.0f} };
	Transform3D transformSphere{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	Transform3D transformSprite { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

	Transform3D cameraTransform { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -10.0f } };

#pragma region ImGuiの初期化
#ifdef USE_IMGUI
	// ImGuiの初期化
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device,
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap,
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
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
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, static_cast<int>(i) + 1);
		textureSrvHandleGPU[i] = GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, static_cast<int>(i) + 1);

		//SRVの生成
		device->CreateShaderResourceView(textures[i].textureResource, &srvDesc, textureSrvHandleCPU);
	}
#pragma endregion

	// ウィンドウのxボタンが押されるまでループ
	while (msg.message != WM_QUIT) {
		// Windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {

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
				ImGui::DragFloat4("Color", &materialData->color.x, 0.01f);

				bool lighting = materialData->enableLighting != 0;
				if (ImGui::Checkbox("Enable Lighting", &lighting)) {
					materialData->enableLighting = lighting;
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
					commandQueue->Signal(fence, ++fenceValue);
					if (fence->GetCompletedValue() < fenceValue) {
						fence->SetEventOnCompletion(fenceValue, fenceEvent);
						WaitForSingleObject(fenceEvent, INFINITE);
					}
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

				ImGui::DragFloat2("Vertex0 texcoord", &vertexDataSprite[0].texcoord.x, 0.2f);
				ImGui::DragFloat2("Vertex1 texcoord", &vertexDataSprite[1].texcoord.x, 0.2f);
				ImGui::DragFloat2("Vertex2 texcoord", &vertexDataSprite[2].texcoord.x, 0.2f);
			
				ImGui::Separator();

				const char* textureItems[] = {
					"uvChecker",
					"monsterBall"
				};

				ImGui::Combo("Texture", reinterpret_cast<int*>(&spriteTextureIndex), textureItems, IM_ARRAYSIZE(textureItems));

				ImGui::PopID();
				ImGui::TreePop();
			}
			ImGui::End();

			ImGui::Begin("Camera");

			ImGui::DragFloat3("scale", &cameraTransform.scale.x, 0.01f);
			ImGui::DragFloat3("rotate", &cameraTransform.rotate.x, 0.01f);
			ImGui::DragFloat3("translate", &cameraTransform.translate.x, 0.01f);
				
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
			Matrix4x4 worldMatrixTriangle = MakeAffineMatrix(transformTriangle.scale, transformTriangle.rotate, transformTriangle.translate);
			Matrix4x4 wvpTriangle = Multiply(worldMatrixTriangle, Multiply(viewMatrix, projectionMatrix));
			transformationMatrixDataTriangle->WVP = wvpTriangle;
			transformationMatrixDataTriangle->World = worldMatrixTriangle;

			// 球
			Matrix4x4 worldMatrixSphere = MakeAffineMatrix(transformSphere.scale, transformSphere.rotate, transformSphere.translate);
			Matrix4x4 wvpSphere = Multiply(worldMatrixSphere, Multiply(viewMatrix, projectionMatrix));
			transformationMatrixDataSphere->WVP = wvpSphere;
			transformationMatrixDataSphere->World = worldMatrixSphere;

			// Sprite用のworldViewProjectionMatrixを作る
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
			transformationMatrixDataSprite->WVP = worldViewProjectionMatrixSprite;
			transformationMatrixDataSprite->World = worldMatrixSprite;
#pragma endregion

#pragma region 画面クリア処理

			// これから書き込むバックバッファのインデックスを取得
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			// TransitionBarrierの設定
			D3D12_RESOURCE_BARRIER barrier{};
			// 今回のバリアはTransition
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			// Noneにしておく
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			// バリアを張る対象のリソース。現在のバックバッファに対して行う
			barrier.Transition.pResource = swapChainResources[backBufferIndex];
			// 遷移前（現在）のResourceState
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			// 遷移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);
			//描画先のRTVとDSVを設定する
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);
			// 指定した色で画面全体をクリアする
			float clearColor[] = { 0.1f, 0.25f, 0.5f,1.0f }; // 青っぽい色。RGBAの順
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
			//指定した深度で画面全体をクリアする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

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
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
			//形状を設定。PSOに設定しているものとはまた別。同じものを設定すると考えておけば良い
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
#pragma endregion

			//
			// ---- 三角形描画 ----
			//
			//SRVのDescriptorTableの先頭を設定。2はrootParameter[2]である。
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU[triangleTextureIndex]);
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewTriangle); // VBVを設定
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceTriangle->GetGPUVirtualAddress()); //wvp用のCBufferの場所を設定
			//描画！(DrawCall/ドローコール)。3頂点で1つのインスタンス。インスタンスについては今後
			commandList->DrawInstanced(6, 1, 0, 0);
			
			//
			// ---- 球描画 ----
			//
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU[sphereTextureIndex]);
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSphere);
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSphere->GetGPUVirtualAddress());
			// 球の描画
			commandList->DrawInstanced(sphereVertexCount, 1, 0, 0);

#pragma endregion

#pragma region 2D描画
			//
			// ---- スプライト描画 ----
			//
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU[spriteTextureIndex]);
			//Spriteの描画。変更が必要なものだけ変更する
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite); // VBVを設定
			// TransformationMatrixCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
			// マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
			//描画！(DrawCall/ドローコール)
			commandList->DrawInstanced(6, 1, 0, 0);
#pragma endregion

#pragma endregion

#ifdef USE_IMGUI
			//実際のcommandListのImGuiの描画コマンドを積む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

#pragma region 画面に映す処理
			// 画面に描く処理はすべて終わり、画面に映すので、状態を遷移
			// 今回はRenderTargetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
			hr = commandList->Close();
			assert(SUCCEEDED(hr));
			// GPUにコマンドリストの実行を行わせる
			ID3D12CommandList* commandLists[] = { commandList };
			commandQueue->ExecuteCommandLists(1, commandLists);
			// GPUとOUに画面の交換を行うよう追加する
			swapChain->Present(1, 0);

			// Fenceの値を更新
			fenceValue++;
			// GPUがここまでたどり着いたときに、Fenceの値を指定した値に代入するようにSignalを送る
			commandQueue->Signal(fence, fenceValue);
			// Fenceの値が指定したSignal値にたどり着いているか確認する
			// GetCompletedValueの初期値はFence作成時に渡した初期値
			if (fence->GetCompletedValue() < fenceValue) {
				// 指定したSignalにたどりついていないので、たどり着くまで待つようにイベントを設定する
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				// イベント待つ
				WaitForSingleObject(fenceEvent, INFINITE);
			}

			// 次のフレーム用のコマンドリストを準備
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator, graphicsPipelineState);
			assert(SUCCEEDED(hr));
#pragma endregion

		}
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
	includeHandler->Release();
	dxcCompiler->Release();
	dxcUtils->Release();
	depthStencilResource->Release();
	dsvDescriptorHeap->Release();
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
	srvDescriptorHeap->Release();
	transformationMatrixResourceSprite->Release();
	transformationMatrixResourceSphere->Release();
	transformationMatrixResourceTriangle->Release();
	CloseHandle(fenceEvent);
	fence->Release();
	rtvDescriptorHeap->Release();
	swapChainResources[0]->Release();
	swapChainResources[1]->Release();
	swapChain->Release();
	commandList->Release();
	materialResource->Release();
	commandAllocator->Release();
	commandQueue->Release();
	device->Release();
	useAdapter->Release();
	dxgiFactory->Release();

#pragma endregion

#ifdef _DEBUG
	debugController->Release();
#endif
	CloseWindow(hwnd);

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