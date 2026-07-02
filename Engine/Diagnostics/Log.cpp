#include "Engine/Diagnostics/Log.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>

namespace {
// InitializeLogFileで開くログファイル（開くまでは出力ウィンドウのみに出す）
std::ofstream logStream;
// Logは別スレッド（D3D12デバッグレイヤーのコールバック等）からも呼ばれるため排他する
std::mutex logMutex;
}  // namespace

void InitializeLogFile() {
	std::filesystem::create_directory("logs");

	// 現在時刻（秒単位・ローカルタイム）をファイル名にする
	auto now = std::chrono::system_clock::now();
	auto nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };
	std::string filePath = std::format("logs/{:%Y%m%d_%H%M%S}.log", localTime);

	logStream.open(filePath);
}

void Log(const std::string& message) {
	std::lock_guard<std::mutex> lock(logMutex);
	OutputDebugStringA((message + "\n").c_str());
	if (logStream.is_open()) {
		logStream << message << std::endl;
	}
}
