#pragma once
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
	// 中身はこれから埋める
	return EXCEPTION_EXECUTE_HANDLER;
};