#pragma once

#include <string>

// ログ出力ユーティリティ。
// InitializeLogFileを呼ぶと logs/ 以下に日時名のログファイルが作られ、
// 以降のLogは出力ウィンドウとファイルの両方へ書き込む（未初期化なら出力ウィンドウのみ）。

// logsディレクトリに日時名のログファイルを作成する（アプリ起動時に1回呼ぶ）
void InitializeLogFile();

// 出力ウィンドウ（とログファイル）に文字列を出す
void Log(const std::string& message);
