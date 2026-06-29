#include "StereoRenderer.h"

#include <cassert>

#include "GpuResource.h"
#include "DescriptorHeapManager.h"
#include "PipelineManager.h"
#include "ShaderCompiler.h"
#include "log.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

using Microsoft::WRL::ComPtr;

void StereoRenderer::Initialize(
    ID3D12Device* device,
    int32_t width,
    int32_t height,
    ID3D12DescriptorHeap* srvDescriptorHeap,
    uint32_t srvStartIndex) {

    device_ = device;
    width_ = width;
    height_ = height;
    srvDescriptorHeap_ = srvDescriptorHeap;
    srvStartIndex_ = srvStartIndex;

    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // ビュー用RTVヒープ（kViewCount個）
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = kViewCount;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_));
    assert(SUCCEEDED(hr));

    // 共有深度用DSVヒープ（1個）
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_));
    assert(SUCCEEDED(hr));

    CreateOffscreenTargets(width, height);
    CreatePipeline();
}

void StereoRenderer::CreateOffscreenTargets(int32_t width, int32_t height) {

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
    desc.Width = static_cast<UINT64>(width);
    desc.Height = static_cast<UINT>(height);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kColorFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    for (uint32_t i = 0; i < kViewCount; ++i) {
        HRESULT hr = device_->CreateCommittedResource(
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

        // SRV（共有ヒープの srvStartIndex_ + i）。合成パスで連続テーブルとして参照する。
        uint32_t srvIndex = srvStartIndex_ + i;
        D3D12_CPU_DESCRIPTOR_HANDLE srvCPU =
            DescriptorHeapManager::GetCPUDescriptorHandle(srvDescriptorHeap_, srvDescriptorSize_, srvIndex);
        viewSRVHandlesGPU_[i] =
            DescriptorHeapManager::GetGPUDescriptorHandle(srvDescriptorHeap_, srvDescriptorSize_, srvIndex);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = kColorFormat;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(viewColorResources_[i].Get(), &srvDesc, srvCPU);
    }

    // 共有深度バッファ（DEPTH_WRITE状態で生成。以降は遷移せず深度ターゲットとして使い回す）
    depthResource_ = CreateDepthStencilTextureResource(device_, width, height);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(
        depthResource_.Get(), &dsvDesc, dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}

void StereoRenderer::CreatePipeline() {

    // --- 合成用ルートシグネチャ（0:ビューSRVテーブル[t0..] / 1:パラメータCBV[b0] / s0:ポイントサンプラ）---
    // バリア／レンチキュラーはネイティブ等倍前提なので、にじませないポイントサンプリングにする。
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.BaseShaderRegister = 0;
    srvRange.NumDescriptors = kViewCount;
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

    // --- 合成パラメータCBuffer（mode等）。毎フレームComposite()で書き込む。---
    compositeParamsResource_ = CreateBufferResource(device_, sizeof(CompositeParams));
    compositeParamsResource_->Map(0, nullptr, reinterpret_cast<void**>(&compositeParamsData_));
    *compositeParamsData_ = CompositeParams{};
}

void StereoRenderer::Resize(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width == width_ && height == height_) {
        return;
    }

    // 古いリソースを解放してから作り直す（RTV/SRV/DSVは既存ヒープへ上書き再生成する）。
    for (auto& resource : viewColorResources_) {
        resource.Reset();
    }
    depthResource_.Reset();

    width_ = width;
    height_ = height;

    CreateOffscreenTargets(width, height);
}

void StereoRenderer::BeginView(ID3D12GraphicsCommandList* commandList, uint32_t viewIndex) {

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

    // 全ビューを RENDER_TARGET → PIXEL_SHADER_RESOURCE へ遷移
    for (uint32_t i = 0; i < kViewCount; ++i) {
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

    // 現在の方式・キャリブレーションをパラメータCBufferへ反映（毎フレーム全GPU完了待ちのため単一バッファで安全）
    compositeParamsData_->mode = static_cast<int32_t>(mode_);
    compositeParamsData_->barrierPitch = barrierPitch_;
    compositeParamsData_->barrierPhase = barrierPhase_;
    compositeParamsData_->swapEyes = swapEyes_ ? 1 : 0;
    compositeParamsData_->lensPitch = lensPitch_;
    compositeParamsData_->lensSlant = lensSlant_;
    compositeParamsData_->lensOffset = lensOffset_;
    compositeParamsData_->viewCount = static_cast<int32_t>(kViewCount);

    commandList->SetGraphicsRootSignature(compositeRootSignature_.Get());
    commandList->SetPipelineState(compositePSO_.Get());

    ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_ };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);
    // ビューSRVは srvStartIndex_ から連続。テーブル先頭にビュー0（左）を渡す。
    commandList->SetGraphicsRootDescriptorTable(0, viewSRVHandlesGPU_[0]);
    commandList->SetGraphicsRootConstantBufferView(1, compositeParamsResource_->GetGPUVirtualAddress());

    // 頂点バッファ無しでフルスクリーン三角形を描画
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}

#ifdef USE_IMGUI
void StereoRenderer::DrawImGui() {
    ImGui::Begin("Stereoscopic");

    const char* modeItems[] = { "Anaglyph", "Left eye only", "Right eye only", "Parallax barrier", "Lenticular" };
    int modeIndex = static_cast<int>(mode_);
    if (ImGui::Combo("Display Mode", &modeIndex, modeItems, IM_ARRAYSIZE(modeItems))) {
        mode_ = static_cast<DisplayMode>(modeIndex);
    }

    ImGui::Text("Views: %u (Anaglyph/Barrier use the outer two)", kViewCount);

    // 左右割り当て／視点順の入れ替え（全方式に効く。立体が逆のときに使う）
    ImGui::Checkbox("Swap Eyes / Reverse Views", &swapEyes_);

    // パララックスバリアのキャリブレーション（物理シートに合わせて調整）
    if (mode_ == DisplayMode::ParallaxBarrier) {
        ImGui::Separator();
        ImGui::Text("Parallax Barrier Calibration");
        ImGui::DragFloat("Pitch (px/cycle)", &barrierPitch_, 0.005f, 1.0f, 16.0f, "%.3f");
        ImGui::DragFloat("Phase (px)", &barrierPhase_, 0.01f, -16.0f, 16.0f, "%.3f");
        ImGui::TextWrapped(
            "Place the physical barrier sheet over the screen, then tune Pitch/Phase until each eye sees a single clean image. Use Swap Eyes if depth is inverted.");
    } else if (mode_ == DisplayMode::Lenticular) {
        ImGui::Separator();
        ImGui::Text("Lenticular Calibration");
        ImGui::DragFloat("Lens Pitch (subpx/lens)", &lensPitch_, 0.01f, 1.0f, 64.0f, "%.3f");
        ImGui::DragFloat("Slant (subpx/row)", &lensSlant_, 0.005f, -8.0f, 8.0f, "%.3f");
        ImGui::DragFloat("Offset (subpx)", &lensOffset_, 0.02f, -64.0f, 64.0f, "%.3f");
        ImGui::TextWrapped(
            "Place the lenticular sheet over the screen, then tune Lens Pitch (slant=0 for vertical lenses) until a clean 3D image appears. To test without a sheet, set a large Lens Pitch to see the views cycle across wide bands. Use Swap Eyes if depth is inverted.");
    } else {
        ImGui::TextWrapped(
            "Anaglyph: red/cyan glasses (red = left). Left/Right eye only: switch to confirm the two viewpoints differ (no glasses needed).");
    }

    ImGui::End();
}
#endif
