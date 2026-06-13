#pragma once

#include <string>
#include <iostream>
#include <Windows.h>

// 出力ウィンドウに文字列を出す
void Log(const std::string& message);
// ストリームと出力ウィンドウの両方に文字列を出す
void Log(std::ostream& os, const std::string& message);