#pragma once

// 出力ウィンドウに文字を出す
void Log(const std::string& message) {
	OutputDebugStringA((message + "\n").c_str());
}

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA((message + "\n").c_str());
}

// 変数から型を推論してくれる
//Log(std::format("enemyHp:{}, texturePath:{}\n", enemyHp, texturePath));
// 
// string->wstring
std::wstring ConvertString(const std::string& str);
// wstring-.string
std::string ConvertString(const std::wstring& str);

// wstring->string
//Log(ConvertString(std::format(L"WSTRING{}\n", wstringValue)));

