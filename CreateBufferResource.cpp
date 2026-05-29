//#include <d3d12.h>
//#include <wrl.h>
//using Microsoft::WRL::ComPtr;
//
//ID3D12Resource* CreateBufferResource(
//    ID3D12Device* device,
//    size_t sizeInBytes
//) {
//    ID3D12Resource* resource = nullptr;
//
//    D3D12_HEAP_PROPERTIES heapProp{};
//    heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
//
//    D3D12_RESOURCE_DESC desc{};
//    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
//    desc.Width = sizeInBytes;
//    desc.Height = 1;
//    desc.DepthOrArraySize = 1;
//    desc.MipLevels = 1;
//    desc.SampleDesc.Count = 1;
//    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
//
//    HRESULT hr = device->CreateCommittedResource(
//        &heapProp,
//        D3D12_HEAP_FLAG_NONE,
//        &desc,
//        D3D12_RESOURCE_STATE_GENERIC_READ,
//        nullptr,
//        IID_PPV_ARGS(&resource)
//    );
//
//    if (FAILED(hr)) {
//        return nullptr;
//    }
//
//    return resource;
//}