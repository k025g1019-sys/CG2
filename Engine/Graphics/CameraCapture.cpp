#include "CameraCapture.h"

#include <cassert>
#include <cstring>
#include <cstddef>

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include "GpuResource.h"
#include "DescriptorHeapManager.h"
#include "PipelineManager.h"
#include "ShaderCompiler.h"
#include "log.h"
#include "Engine/Input/FaceTracker.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

void CameraCapture::Initialize(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, uint32_t srvIndex) {
    device_ = device;
    srvHeap_ = srvHeap;
    srvIndex_ = srvIndex;
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // ここではカメラを開かない。実カメラ表示が有効化され、UpdateTexture()が初めて呼ばれた時に開く。
}

void CameraCapture::StartCapture() {
    // 表示の有無に関わらずワーカーを起動する（遅延起動・冪等）。視線追跡だけのときも使う。
    if (!workerStarted_) {
        stop_.store(false);
        worker_ = std::thread(&CameraCapture::WorkerThread, this);
        workerStarted_ = true;
    }
}

void CameraCapture::Finalize() {
    stop_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    // 早期に解放しておく（リソースリークチェックより前に確実に消す）。
    blitParamsData_ = nullptr;
    uploadPtr_ = nullptr;
    blitParamsResource_.Reset();
    blitPSO_.Reset();
    blitRootSignature_.Reset();
    upload_.Reset();
    texture_.Reset();
}

void CameraCapture::CreateTextureIfNeeded() {
    if (textureCreated_) {
        return;
    }
    if (!dimsReady_.load()) {
        return;
    }

    int w = 0;
    int h = 0;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        w = frameWidth_;
        h = frameHeight_;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    // --- テクスチャ本体（DEFAULT, TYPELESS）。SRVはsRGBで読む（sRGBバックバッファと相殺し見た目維持）---
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT64>(w);
    desc.Height = static_cast<UINT>(h);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device_->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&texture_));
    if (FAILED(hr)) {
        return;  // 生成失敗。無効のまま（描画されない）。
    }
    textureState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // --- アップロードバッファ（行ピッチは256境界）---
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint_, &numRows, &rowSizeBytes, &totalBytes);
    uploadRowPitch_ = footprint_.Footprint.RowPitch;

    upload_ = CreateBufferResource(device_, static_cast<size_t>(totalBytes));
    upload_->Map(0, nullptr, reinterpret_cast<void**>(&uploadPtr_));
    // 最初のカメラフレーム到着までの表示用にダークグレーで初期化する。
    std::memset(uploadPtr_, 0x20, static_cast<size_t>(totalBytes));
    needsCopy_ = true;

    // --- SRV（共有ヒープのsrvIndex_）---
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu =
        DescriptorHeapManager::GetCPUDescriptorHandle(srvHeap_, srvDescriptorSize_, srvIndex_);
    srvGpu_ = DescriptorHeapManager::GetGPUDescriptorHandle(srvHeap_, srvDescriptorSize_, srvIndex_);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(texture_.Get(), &srvDesc, srvCpu);

    CreateBlitPipeline();

    textureCreated_ = true;
    available_.store(true);
}

void CameraCapture::CreateBlitPipeline() {
    // --- ルートシグネチャ（0:SRVテーブル[t0] / 1:パラメータCBV[b0] / s0:リニアClampサンプラ）---
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.BaseShaderRegister = 0;
    srvRange.NumDescriptors = 1;
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].DescriptorTable.pDescriptorRanges = &srvRange;
    params[0].DescriptorTable.NumDescriptorRanges = 1;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].Descriptor.ShaderRegister = 0;  // b0

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rsDesc.pParameters = params;
    rsDesc.NumParameters = _countof(params);
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.NumStaticSamplers = 1;

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        }
        assert(false);
        return;
    }
    hr = device_->CreateRootSignature(
        0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&blitRootSignature_));
    assert(SUCCEEDED(hr));

    // --- シェーダー（フルスクリーン三角形VSを共用＋カメラブリットPS）---
    ComPtr<IDxcBlob> vertexShaderBlob =
        ShaderCompiler::GetInstance()->Compile(L"Shaders/Fullscreen.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);
    ComPtr<IDxcBlob> pixelShaderBlob =
        ShaderCompiler::GetInstance()->Compile(L"Shaders/CameraBlit.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = false;

    PipelineManager::PipelineConfig config{};
    config.device = device_;
    config.rootSignature = blitRootSignature_.Get();
    config.inputLayout = {};
    config.blendDesc = blendDesc;
    config.rasterizerDesc = rasterizerDesc;
    config.depthStencilDesc = depthStencilDesc;
    config.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    config.dsvFormat = DXGI_FORMAT_UNKNOWN;
    config.vertexShader = vertexShaderBlob;
    config.pixelShader = pixelShaderBlob;
    blitPSO_ = PipelineManager::CreateGraphicsPipeline(config);

    blitParamsResource_ = CreateBufferResource(device_, sizeof(BlitParams));
    blitParamsResource_->Map(0, nullptr, reinterpret_cast<void**>(&blitParamsData_));
    *blitParamsData_ = BlitParams{};
}

void CameraCapture::Transition(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES after) {
    if (textureState_ == after) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture_.Get();
    barrier.Transition.StateBefore = textureState_;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    textureState_ = after;
}

void CameraCapture::UpdateTexture(ID3D12GraphicsCommandList* commandList) {
    // 実カメラ表示が有効化された最初のフレームで、カメラ取得スレッドを起動する（遅延起動）。
    StartCapture();

    CreateTextureIfNeeded();
    if (!textureCreated_) {
        return;
    }

    // 新フレームがあればアップロードバッファへ詰め直す（行ピッチ256境界に合わせる）。
    if (newFrame_.exchange(false)) {
        std::lock_guard<std::mutex> lock(frameMutex_);
        const int w = frameWidth_;
        const int h = frameHeight_;
        const size_t srcPitch = static_cast<size_t>(w) * 4;
        if (cpuFrame_.size() >= srcPitch * static_cast<size_t>(h)) {
            const uint8_t* src = cpuFrame_.data();
            for (int y = 0; y < h; ++y) {
                std::memcpy(
                    uploadPtr_ + static_cast<size_t>(y) * uploadRowPitch_,
                    src + static_cast<size_t>(y) * srcPitch,
                    srcPitch);
            }
        }
        needsCopy_ = true;
    }

    if (!needsCopy_) {
        return;
    }
    needsCopy_ = false;

    Transition(commandList, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = texture_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload_.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint_;

    commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    Transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void CameraCapture::Draw(
    ID3D12GraphicsCommandList* commandList,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissorRect) {
    if (!textureCreated_) {
        return;
    }

    // レターボックス：カメラ縦横比を矩形内へ「contain」で収める（歪み防止。余白は黒）。
    const float vpAspect = (viewport.Height > 0.0f) ? (viewport.Width / viewport.Height) : 1.0f;
    const float camAspect = (frameHeight_ > 0)
        ? static_cast<float>(frameWidth_) / static_cast<float>(frameHeight_)
        : vpAspect;
    float fitX = 1.0f;
    float fitY = 1.0f;
    if (camAspect > vpAspect) {
        fitY = vpAspect / camAspect;  // カメラが横長 → 上下に黒帯
    } else {
        fitX = camAspect / vpAspect;  // カメラが縦長 → 左右に黒帯
    }
    blitParamsData_->fitX = fitX;
    blitParamsData_->fitY = fitY;
    blitParamsData_->offsetX = (1.0f - fitX) * 0.5f;
    blitParamsData_->offsetY = (1.0f - fitY) * 0.5f;
    blitParamsData_->mirror = 1;  // 内蔵カメラは鏡像のほうが自然

    commandList->OMSetRenderTargets(1, &rtv, false, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    commandList->SetGraphicsRootSignature(blitRootSignature_.Get());
    commandList->SetPipelineState(blitPSO_.Get());
    ID3D12DescriptorHeap* heaps[] = { srvHeap_ };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootDescriptorTable(0, srvGpu_);
    commandList->SetGraphicsRootConstantBufferView(1, blitParamsResource_->GetGPUVirtualAddress());

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}

void CameraCapture::WorkerThread() {
    // MFオブジェクトはこのスレッドで生成・使用する（スレッドアフィニティに敏感なため）。
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool mfStarted = false;
    ComPtr<IMFSourceReader> reader;

    do {
        if (FAILED(MFStartup(MF_VERSION))) {
            break;
        }
        mfStarted = true;

        // --- ビデオキャプチャデバイスを列挙して先頭を使う ---
        ComPtr<IMFAttributes> attr;
        if (FAILED(MFCreateAttributes(&attr, 1))) {
            break;
        }
        attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        if (FAILED(MFEnumDeviceSources(attr.Get(), &devices, &count)) || count == 0) {
            if (devices) {
                CoTaskMemFree(devices);
            }
            break;  // カメラ無し
        }
        ComPtr<IMFMediaSource> source;
        HRESULT hr = devices[0]->ActivateObject(IID_PPV_ARGS(&source));
        for (UINT32 i = 0; i < count; ++i) {
            devices[i]->Release();
        }
        CoTaskMemFree(devices);
        if (FAILED(hr)) {
            break;
        }

        // --- ソースリーダー（フォーマット変換を有効化してRGB32を得る）---
        ComPtr<IMFAttributes> readerAttr;
        MFCreateAttributes(&readerAttr, 1);
        readerAttr->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (FAILED(MFCreateSourceReaderFromMediaSource(source.Get(), readerAttr.Get(), &reader))) {
            break;
        }

        // ストリーム指定の列挙値はsigned扱いなので、DWORD引数へ渡す前に明示変換しておく。
        const DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

        // --- 出力フォーマットをRGB32（=BGRA32）へ ---
        ComPtr<IMFMediaType> outType;
        MFCreateMediaType(&outType);
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED(reader->SetCurrentMediaType(kVideoStream, nullptr, outType.Get()))) {
            break;
        }

        // --- 実際の解像度とデフォルトストライド（向き判定用）を取得 ---
        ComPtr<IMFMediaType> curType;
        if (FAILED(reader->GetCurrentMediaType(kVideoStream, &curType))) {
            break;
        }
        UINT32 w = 0;
        UINT32 h = 0;
        if (FAILED(MFGetAttributeSize(curType.Get(), MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0) {
            break;
        }
        LONG defaultStride = static_cast<LONG>(w) * 4;
        {
            UINT32 strideAttr = 0;
            if (SUCCEEDED(curType->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideAttr))) {
                defaultStride = static_cast<LONG>(static_cast<INT32>(strideAttr));
            }
        }

        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            frameWidth_ = static_cast<int>(w);
            frameHeight_ = static_cast<int>(h);
            cpuFrame_.assign(static_cast<size_t>(w) * h * 4, 0x20);
        }
        dimsReady_.store(true);

        // --- フレーム取得ループ ---
        std::vector<uint8_t> work(static_cast<size_t>(w) * h * 4);
        const size_t dstPitch = static_cast<size_t>(w) * 4;

        while (!stop_.load()) {
            DWORD actualStream = 0;
            DWORD streamFlags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(
                kVideoStream, 0,
                &actualStream, &streamFlags, &timestamp, &sample);
            if (FAILED(hr)) {
                break;
            }
            if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
                break;
            }
            if (!sample) {
                continue;  // タイムアウト等。次のサンプルを待つ。
            }

            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
                continue;
            }

            // できればIMF2DBufferでピッチ込みロック（向きはピッチ符号で正しく処理できる）。
            ComPtr<IMF2DBuffer> buffer2d;
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            bool locked2d = false;
            BYTE* lockedPtr = nullptr;

            if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch))) {
                locked2d = true;
            } else {
                DWORD maxLen = 0;
                DWORD curLen = 0;
                if (FAILED(buffer->Lock(&lockedPtr, &maxLen, &curLen))) {
                    continue;
                }
                // デフォルトストライドの符号で上下を合わせ、top-downで読めるようにする。
                if (defaultStride < 0) {
                    scan0 = lockedPtr + static_cast<size_t>(h - 1) * static_cast<size_t>(-defaultStride);
                    pitch = defaultStride;  // 負
                } else {
                    scan0 = lockedPtr;
                    pitch = defaultStride;
                }
            }

            // top-down・隙間なしのBGRAへ詰める（scan0 + y*pitch で常に上から下へ）。
            for (UINT32 y = 0; y < h; ++y) {
                const BYTE* row = scan0 + static_cast<ptrdiff_t>(y) * pitch;
                std::memcpy(work.data() + static_cast<size_t>(y) * dstPitch, row, dstPitch);
            }

            if (locked2d) {
                buffer2d->Unlock2D();
            } else {
                buffer->Unlock();
            }

            {
                std::lock_guard<std::mutex> lock(frameMutex_);
                if (cpuFrame_.size() == work.size()) {
                    std::memcpy(cpuFrame_.data(), work.data(), work.size());
                }
            }
            newFrame_.store(true);

            // 顔検出が連携されていれば、このフレーム（top-down BGRA）を渡す（内部で時間間引きする）。
            // このスレッドはCOM(MTA)初期化済みなのでWinRTの顔検出をそのまま呼べる。
            if (faceTracker_ != nullptr) {
                faceTracker_->ProcessFrameBGRA(work.data(), static_cast<int>(w), static_cast<int>(h));
            }
        }
    } while (false);

    reader.Reset();
    if (mfStarted) {
        MFShutdown();
    }
    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }
}
