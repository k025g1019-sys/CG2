#pragma once

#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>
#include <wrl.h>
#include "externals/DirectXTex/DirectXTex.h"

/// <summary>
/// テクスチャの読み込み・GPU転送・SRV作成・キャッシュを行うシングルトン。
/// Loadが返すハンドルをGetSrvHandleGPUに渡して描画に使う。
/// 同じパスを複数回Loadしても読み込みは1回だけ行われ、同じハンドルが返る。
/// </summary>
class TextureManager {
public:

    static TextureManager* GetInstance();

    // デバイスと、転送コマンドを積むコマンドリストを登録する
    // （DescriptorHeapManager::Initializeの後に呼ぶ）
    void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// テクスチャを読み込み、GPU転送コマンド発行とSRV作成まで行ってハンドルを返す。
    /// 転送コマンドはコマンドリストに積まれ、次のExecuteCommandListsで実行される。
    /// </summary>
    /// <param name="filepath">実行ディレクトリからの相対パス</param>
    /// <returns>GetSrvHandleGPUに渡すテクスチャハンドル</returns>
    uint32_t Load(const std::string& filepath);

    // 描画に使うSRVのGPUハンドルを取得する
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(uint32_t textureHandle) const;

    // 全テクスチャを解放する（リソースリークチェックより前に呼ぶ）
    void Finalize();

private:

    TextureManager() = default;

    ~TextureManager() = default;

    TextureManager(const TextureManager&) = delete;

    TextureManager& operator=(const TextureManager&) = delete;

private:

    // 読み込み済みテクスチャ1枚分のデータ
    struct Texture {
        std::string filepath;
        DirectX::TexMetadata metadata{};
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        // 転送コマンドが実行されるまでGPUが参照する中間リソース（Finalizeまで保持）
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediate;
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU{};
    };

    // ファイルからミップマップ付きイメージを読み込む
    static DirectX::ScratchImage LoadTextureImage(const std::string& filepath);

    // メタデータからGPU上のテクスチャリソースを作成する
    static Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
        ID3D12Device* device,
        const DirectX::TexMetadata& metadata);

    // イメージをGPUへ転送するコマンドを積む（戻り値は転送用の中間リソース）
    static Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
        ID3D12Resource* texture,
        const DirectX::ScratchImage& mipImages,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList);

private:

    ID3D12Device* device_ = nullptr;                       // 非所有

    ID3D12GraphicsCommandList* commandList_ = nullptr;     // 非所有

    std::vector<Texture> textures_;  // ハンドル＝このvectorのindex
};
