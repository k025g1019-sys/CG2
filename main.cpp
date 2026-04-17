#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <filesystem>
// ファイルに書いたり読んだりするライブラリ
#include <fstream>
// 時間を扱うライブラリ
#include <chrono>
#include "ConvertString.h"

//#include <d3d12.h>
//#include <dxgi1_6.h>
//#include <cassert>
//#pragma comment(lib, "d3d12.lib")
//#pragma comment(lib, "dxgi.lib")

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA((message + "\n").c_str());
}

// 出力ウィンドウに文字を出す
//void Log(const std::string& message) {
//	OutputDebugStringA(message.c_str());
//}

// 変数から型を推論してくれる
//Log(std::format("enemyHp:{}, texturePath:{}\n", enemyHp, texturePath));

// wstring->string
//Log(ConvertString(std::format(L"WSTRING{}\n", wstringValue)));

// string->wstring
std::wstring ConvertString(const std::string& str);
// wstring-.string
std::string ConvertString(const std::wstring& str);

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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

// DXGIファクトリーの生成
//IDXGIFactory7* dxgiFactory = nullptr;
//HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
//assert(SUCCEEDED(hr));
//
//IDXGIAdapter4* useAdapter = nullptr;
//
//for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i,
//	DXGI_GPU_PREFERENCE_HIGH_PREFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
//	DXGI_ERROR_NOT_FOUND; ++i) {
//
//	DXGI_ADAPTER_DESC3 adapterDesc{};
//	hr = useAdapter->GetDesc3(&adapterDesc);
//	assert(SUCCEEDED(hr));
//}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

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

	// 出力ウィンドウへの文字出力
	OutputDebugStringA("Hello,DirectX!\n");

	// 基本的な使い方
	// 文字列を格納する
	std::string str0{"STRING!!!"};

	// 整数を文字列にする
	std::string str1{std::to_string(10)};

	Log(logStream, "Program started");
	logStream.flush();

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

	// クライアント領域のサイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	// ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };

	// クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウの生成
	HWND hwnd = CreateWindow(
		wc.lpszClassName,	  // 利用するクラス名
		L"Window",				  // タイトルバーの文字
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
	// ウィンドウのxボタンが押されるまでループ
	while (msg.message != WM_QUIT) {
		// Windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			// ゲームの処理
		}
	}

	return 0;
}