#include "Engine/Diagnostics/Log.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>

namespace {
// InitializeLogFileで開くログファイル（開くまでは出力ウィンドウのみに出す）
std::ofstream logStream;
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
	OutputDebugStringA((message + "\n").c_str());
	if (logStream.is_open()) {
		logStream << message << std::endl;
	}
}
