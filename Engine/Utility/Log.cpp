#include "Log.h"

// 出力ウィンドウに文字を出す
void Log(const std::string& message) {
    OutputDebugStringA((message + "\n").c_str());
}

void Log(std::ostream& os, const std::string& message) {
    os << message << std::endl;
    OutputDebugStringA((message + "\n").c_str());
}