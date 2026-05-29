#pragma once

#include <Windows.h>
#include <wrl/client.h>
#include <dxcapi.h>

#include <string>

class ShaderCompiler {
public:

    static ShaderCompiler* GetInstance();

    void Initialize();

    void Finalize();

    IDxcBlob* Compile(
        const std::wstring& filePath,
        const wchar_t* profile
    );

private:

    ShaderCompiler() = default;

    ~ShaderCompiler() = default;

    ShaderCompiler(const ShaderCompiler&) = delete;

    ShaderCompiler& operator=(const ShaderCompiler&) = delete;

private:

    IDxcUtils* dxcUtils_ = nullptr;

    IDxcCompiler3* dxcCompiler_ = nullptr;

    IDxcIncludeHandler* includeHandler_ = nullptr;
};