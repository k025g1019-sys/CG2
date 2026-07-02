#include "Engine/Graphics/StereoRenderer.h"

#include <cassert>

#include "Engine/Core/DirectXCore.h"
#include "Engine/Graphics/DescriptorHeapManager.h"
#include "Engine/Graphics/GpuResource.h"
#include "Engine/Graphics/PipelineManager.h"
#include "Engine/Graphics/ShaderCompiler.h"
#include "Engine/Diagnostics/Log.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

using Microsoft::WRL::ComPtr;

StereoRenderer* StereoRenderer::GetInstance() {
    static StereoRenderer instance;
    return &instance;
}

void StereoRenderer::Initialize(ID3D12Device* device, int32_t width, int32_t height) {

    device_ = device;
    width_ = width;
    height_ = height;

    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // ビュー用RTVヒープ（kMaxViewCount個）と共有深度用DSVヒープ（1個）。
    // ヒープ自体は小さいので先に確保しておく（ターゲット本体は遅延確保）。
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = kMaxViewCount;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    [[maybe_unused]] HRESULT hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_));
    assert(SUCCEEDED(hr));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_));
    assert(SUCCEEDED(hr));

    // 合成PSが連続テーブルとして参照するSRVスロットをまとめて予約する
    DescriptorHeapManager* heapManager = DescriptorHeapManager::GetInstance();
    srvStartIndex_ = heapManager->AllocateSrv();
    for (uint32_t i = 1; i < kMaxViewCount; ++i) {
        [[maybe_unused]] uint32_t index = heapManager->AllocateSrv();
        assert(index == srvStartIndex_ + i && "ビューSRVは連続スロットが必要");
    }
    viewSRVTableStart_ = heapManager->GetSrvGPUHandle(srvStartIndex_);

    CreatePipeline();
}

void StereoRenderer::Finalize() {
    ReleaseTargets();
    paramsCB_.Reset();
    compositePSO_.Reset();
    compositeRootSignature_.Reset();
    dsvHeap_.Reset();
    rtvHeap_.Reset();
    device_ = nullptr;
}

uint32_t StereoRenderer::GetActiveViewCount() const {
    switch (mode_) {
    case DisplayMode::kOff:
        return 1;  // 通常描画（中心カメラのみ）
    case DisplayMode::kLenticular:
        return kMaxViewCount;
    default:
        return 2;  // アナグリフ／バリア／左右単独は左右の2視点で足りる
    }
}

void StereoRenderer::Resize(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width == width_ && height == height_) {
        return;
    }

    width_ = width;
    height_ = height;

    // 確保済みなら解放して、次のBeginViewで新サイズで作り直す
    // （呼び出し側=FrameworkがDirectXCore::ResizeでGPU完了待ち済み）。
    ReleaseTargets();
}

void StereoRenderer::EnsureTargets(uint32_t viewCount) {
    assert(viewCount <= kMaxViewCount);
    if (allocatedViewCount_ >= viewCount) {
        return;
    }

    // クリア最適値（BeginFrameのバックバッファと同じ色にしておく）
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kColorFormat;
    clearValue.Color[0] = 0.1f;
    clearValue.Color[1] = 0.25f;
    clearValue.Color[2] = 0.5f;
    clearValue.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;  // VRAM上

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT64>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kColorFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    DescriptorHeapManager* heapManager = DescriptorHeapManager::GetInstance();

    // 不足分のターゲットを作る（確保済みの分は使い回す）
    for (uint32_t i = allocatedViewCount_; i < viewCount; ++i) {
        [[maybe_unused]] HRESULT hr = device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,  // BeginViewでそのまま描き始められる状態で作る
            &clearValue,
            IID_PPV_ARGS(&viewColorResources_[i]));
        assert(SUCCEEDED(hr));
        viewStates_[i] = D3D12_RESOURCE_STATE_RENDER_TARGET;

        // RTV（ビュー用RTVヒープのi番目）
        viewRTVHandles_[i] = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        viewRTVHandles_[i].ptr += static_cast<SIZE_T>(rtvDescriptorSize_) * i;
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = kColorFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device_->CreateRenderTargetView(viewColorResources_[i].Get(), &rtvDesc, viewRTVHandles_[i]);
    }
    allocatedViewCount_ = viewCount;

    // SRVを予約済みスロットへ作成する。
    // 未使用スロット（viewCount..kMaxViewCount-1）は、シェーダーが参照しなくても
    // ディスクリプタとして有効になるよう最後のターゲットへのSRVで埋めておく。
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = kColorFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    for (uint32_t i = 0; i < kMaxViewCount; ++i) {
        uint32_t sourceIndex = (i < allocatedViewCount_) ? i : (allocatedViewCount_ - 1);
        device_->CreateShaderResourceView(
            viewColorResources_[sourceIndex].Get(), &srvDesc,
            heapManager->GetSrvCPUHandle(srvStartIndex_ + i));
    }

    // 共有深度バッファ（DEPTH_WRITE状態で生成。以降は遷移せず深度ターゲットとして使い回す）
    if (!depthResource_) {
        depthResource_ = CreateDepthStencilTextureResource(device_, width_, height_);
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->CreateDepthStencilView(
            depthResource_.Get(), &dsvDesc, dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    }
}

void StereoRenderer::ReleaseTargets() {
    for (auto& resource : viewColorResources_) {
        resource.Reset();
    }
    depthResource_.Reset();
    allocatedViewCount_ = 0;
}

void StereoRenderer::CreatePipeline() {

    // --- 合成用ルートシグネチャ（0:ビューSRVテーブル[t0..] / 1:パラメータCBV[b0] / s0:ポイントサンプラ）---
    // バリア／レンチキュラーはネイティブ等倍前提なので、にじませないポイントサンプリングにする。
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.BaseShaderRegister = 0;
    srvRange.NumDescriptors = kMaxViewCount;
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Descriptor.ShaderRegister = 0;  // b0

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;  // 入力アセンブラ不使用
    rsDesc.pParameters = rootParameters;
    rsDesc.NumParameters = _countof(rootParameters);
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.NumStaticSamplers = 1;

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        assert(false);
    }
    hr = device_->CreateRootSignature(
        0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&compositeRootSignature_));
    assert(SUCCEEDED(hr));

    // --- シェーダー（実行時DXCコンパイル。フルスクリーン三角形VS＋合成PS）---
    ComPtr<IDxcBlob> vertexShaderBlob =
        ShaderCompiler::GetInstance()->Compile(L"Shaders/Fullscreen.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);
    ComPtr<IDxcBlob> pixelShaderBlob =
        ShaderCompiler::GetInstance()->Compile(L"Shaders/Composite.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    // --- PSO（入力レイアウト無し・カリング無し・深度無し）---
    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = false;

    PipelineManager::PipelineConfig config{};
    config.device = device_;
    config.rootSignature = compositeRootSignature_.Get();
    config.inputLayout = {};  // 頂点バッファ不使用（SV_VertexIDで生成）
    config.blendDesc = blendDesc;
    config.rasterizerDesc = rasterizerDesc;
    config.depthStencilDesc = depthStencilDesc;
    config.rtvFormat = kColorFormat;
    config.dsvFormat = DXGI_FORMAT_UNKNOWN;  // 深度無し
    config.vertexShader = vertexShaderBlob;
    config.pixelShader = pixelShaderBlob;

    compositePSO_ = PipelineManager::CreateGraphicsPipeline(config);

    // --- 合成パラメータCBuffer（フレームインフライトぶんのスロットを持つ）---
    paramsCB_.Create(device_, DirectXCore::kFramesInFlight);
}

void StereoRenderer::BeginView(ID3D12GraphicsCommandList* commandList, uint32_t viewIndex) {

    // オフスクリーンが未確保（Offから切り替えた直後・リサイズ直後）ならここで確保する
    EnsureTargets(GetActiveViewCount());
    assert(viewIndex < allocatedViewCount_);

    // SRV（前フレームの合成後）→ RENDER_TARGET へ遷移
    if (viewStates_[viewIndex] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = viewColorResources_[viewIndex].Get();
        barrier.Transition.StateBefore = viewStates_[viewIndex];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
        viewStates_[viewIndex] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(1, &viewRTVHandles_[viewIndex], false, &dsvHandle);

    // ビューポート／シザーはオフスクリーンの解像度に合わせる
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissorRect{ 0, 0, width_, height_ };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
    commandList->ClearRenderTargetView(viewRTVHandles_[viewIndex], clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void StereoRenderer::Composite(
    ID3D12GraphicsCommandList* commandList,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissorRect) {

    const uint32_t viewCount = GetActiveViewCount();
    assert(viewCount <= allocatedViewCount_);

    // 使用中の全ビューを RENDER_TARGET → PIXEL_SHADER_RESOURCE へ遷移
    for (uint32_t i = 0; i < viewCount; ++i) {
        if (viewStates_[i] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = viewColorResources_[i].Get();
            barrier.Transition.StateBefore = viewStates_[i];
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);
            viewStates_[i] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }

    // 出力先をバックバッファへ（深度は使わない）
    commandList->OMSetRenderTargets(1, &backBufferRTV, false, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    // 現在の方式・キャリブレーションをパラメータCBufferの現フレームスロットへ書き込む
    CompositeParams params{};
    params.mode = static_cast<int32_t>(mode_);
    params.barrierPitch = barrierPitch_;
    params.barrierPhase = barrierPhase_;
    params.swapEyes = swapEyes_ ? 1 : 0;
    params.lensPitch = lensPitch_;
    params.lensSlant = lensSlant_;
    params.lensOffset = lensOffset_;
    params.viewCount = static_cast<int32_t>(viewCount);
    params.ghostReduction = ghostReduction_;
    params.anaglyphGray = anaglyphGray_ ? 1 : 0;

    uint32_t frameIndex = DirectXCore::GetInstance()->GetFrameIndex();
    paramsCB_.Write(frameIndex, params);

    commandList->SetGraphicsRootSignature(compositeRootSignature_.Get());
    commandList->SetPipelineState(compositePSO_.Get());

    // ビューSRVは予約済みスロットに連続で並んでいる（テーブル先頭にビュー0=左）
    commandList->SetGraphicsRootDescriptorTable(0, viewSRVTableStart_);
    commandList->SetGraphicsRootConstantBufferView(1, paramsCB_.GetGPUAddress(frameIndex));

    // 頂点バッファ無しでフルスクリーン三角形を描画
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}

#ifdef USE_IMGUI
void StereoRenderer::DrawImGui() {
    ImGui::Begin("Stereoscopic");

    const char* modeItems[] = {
        "Off (normal rendering)", "Anaglyph", "Left eye only", "Right eye only",
        "Parallax barrier", "Lenticular"
    };
    // Comboのindex 0..5 を DisplayMode -1..4 に対応させる
    int modeIndex = static_cast<int>(mode_) + 1;
    if (ImGui::Combo("Display Mode", &modeIndex, modeItems, IM_ARRAYSIZE(modeItems))) {
        DisplayMode newMode = static_cast<DisplayMode>(modeIndex - 1);
        if (newMode != mode_) {
            mode_ = newMode;
            // Offへ戻したらオフスクリーンを解放してVRAMも使わない状態にする
            // （実行中のフレームが参照している可能性があるため、GPU完了を待ってから解放する）
            if (mode_ == DisplayMode::kOff && allocatedViewCount_ > 0) {
                DirectXCore::GetInstance()->WaitForGPU();
                ReleaseTargets();
            }
        }
    }

    if (mode_ == DisplayMode::kOff) {
        ImGui::TextWrapped(
            "Stereo rendering is OFF: the scene renders once directly to the back buffer "
            "(no offscreen targets are allocated).");
        ImGui::End();
        return;
    }

    ImGui::Text("Views: %u (allocated: %u)", GetActiveViewCount(), allocatedViewCount_);

    // 左右割り当て／視点順の入れ替え（全方式に効く。立体が逆のときに使う）
    ImGui::Checkbox("Swap Eyes / Reverse Views", &swapEyes_);

    // パララックスバリアのキャリブレーション（物理シートに合わせて調整）
    if (mode_ == DisplayMode::kParallaxBarrier) {
        ImGui::Separator();
        ImGui::Text("Parallax Barrier Calibration");
        ImGui::DragFloat("Pitch (px/cycle)", &barrierPitch_, 0.005f, 1.0f, 16.0f, "%.3f");
        ImGui::DragFloat("Phase (px)", &barrierPhase_, 0.01f, -16.0f, 16.0f, "%.3f");
        ImGui::TextWrapped(
            "Place the physical barrier sheet over the screen, then tune Pitch/Phase until each eye sees a single clean image. Use Swap Eyes if depth is inverted.");
    } else if (mode_ == DisplayMode::kLenticular) {
        ImGui::Separator();
        ImGui::Text("Lenticular Calibration");
        ImGui::DragFloat("Lens Pitch (subpx/lens)", &lensPitch_, 0.01f, 1.0f, 64.0f, "%.3f");
        ImGui::DragFloat("Slant (subpx/row)", &lensSlant_, 0.005f, -8.0f, 8.0f, "%.3f");
        ImGui::DragFloat("Offset (subpx)", &lensOffset_, 0.02f, -64.0f, 64.0f, "%.3f");
        ImGui::TextWrapped(
            "Place the lenticular sheet over the screen, then tune Lens Pitch (slant=0 for vertical lenses) until a clean 3D image appears. To test without a sheet, set a large Lens Pitch to see the views cycle across wide bands. Use Swap Eyes if depth is inverted.");
    } else if (mode_ == DisplayMode::kAnaglyph) {
        ImGui::Separator();
        ImGui::Text("Anaglyph Ghost Reduction");
        // 赤フィルタから右眼像（シアン）が漏れて二重に見えるのを軽減する。
        // メガネを掛けた状態で、赤側のゴーストが目立たなくなるまで上げる。
        ImGui::SliderFloat("Ghost Reduction", &ghostReduction_, 0.0f, 0.5f, "%.2f");
        ImGui::Checkbox("Gray Anaglyph (no color)", &anaglyphGray_);
        ImGui::TextWrapped(
            "Anaglyph: red/cyan glasses (red = left). If objects look doubled through the RED lens, "
            "raise Ghost Reduction (cancels cyan light leaking through the red filter), "
            "and set Convergence (Camera window) to the distance of your subject. "
            "Gray Anaglyph further reduces ghosting and color rivalry.");
    } else {
        ImGui::TextWrapped(
            "Left/Right eye only: switch to confirm the two viewpoints differ (no glasses needed).");
    }

    ImGui::End();
}
#endif
