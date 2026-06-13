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

    Microsoft::WRL::ComPtr<IDxcBlob> Compile(
        const std::wstring& filePath,
        const wchar_t* profile
    );

private:

    ShaderCompiler() = default;

    ~ShaderCompiler() = default;

    ShaderCompiler(const ShaderCompiler&) = delete;

    ShaderCompiler& operator=(const ShaderCompiler&) = delete;

private:

    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;

    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;

    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;
};