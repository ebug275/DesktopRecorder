#include "pch.h"
#include "Duplicator.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

Duplicator::Duplicator()
{
    m_Device = nullptr;
    m_Context = nullptr;
    m_DeskDupl = nullptr;
    m_AcquiredDesktopImage = nullptr;

    ZeroMemory(&m_DesktopDesc, sizeof(DXGI_OUTPUT_DESC));
    ZeroMemory(&m_DuplicationDesc, sizeof(DXGI_OUTDUPL_DESC));

    m_width = 0;
    m_height = 0;
    m_bytepixel = 0;
    m_frame_buffer_len = 0;
    m_frame_buffer = nullptr;
    m_capture_running = false;
    m_fps = 0;
}

Duplicator::~Duplicator()
{
    stop_duplicate();

    if (m_frame_buffer)
    {
        delete[] m_frame_buffer;
        m_frame_buffer = nullptr;
    }

    if (m_AcquiredDesktopImage)
    {
        m_AcquiredDesktopImage->Release();
        m_AcquiredDesktopImage = nullptr;
    }

    if (m_DeskDupl)
    {
        m_DeskDupl->Release();
        m_DeskDupl = nullptr;
    }

    if (m_Context)
    {
        m_Context->Release();
        m_Context = nullptr;
    }

    if (m_Device)
    {
        m_Device->Release();
        m_Device = nullptr;
    }
}

HRESULT Duplicator::initialize(const wchar_t* target_display, int32_t fps)
{
    HRESULT hr = S_OK;

    // Driver types supported
    D3D_DRIVER_TYPE DriverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

    // Feature levels supported
    D3D_FEATURE_LEVEL FeatureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_1
    };
    UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

    D3D_FEATURE_LEVEL FeatureLevel;

    // Create device
    for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
    {
        hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, 0, FeatureLevels, NumFeatureLevels,
            D3D11_SDK_VERSION, &m_Device, &FeatureLevel, &m_Context);
        if (SUCCEEDED(hr))
        {
            // Device creation success, no need to loop anymore
            break;
        }
    }
    if (FAILED(hr))
    {
        TRACE(_T("Failed to create device in InitializeDx hr: 0x%x\n"), hr);
        return hr;
    }

    // Get DXGI device
    IDXGIDevice* DxgiDevice = nullptr;
    hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
    if (FAILED(hr))
    {
        TRACE(_T("Failed to QI for DXGI Device hr: 0x%x\n"), hr);
        return hr;
    }

    // Get DXGI adapter
    IDXGIAdapter* DxgiAdapter = nullptr;
    hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
    DxgiDevice->Release();
    DxgiDevice = nullptr;
    if (FAILED(hr))
    {
        TRACE(_T("Failed to get parent DXGI Adapter hr: 0x%x\n"), hr);
        return hr;
    }

    // Get output
    IDXGIOutput* DxgiOutput = nullptr;
    UINT OutputCount = 0;
    while (SUCCEEDED(hr))
    {
        if (DxgiOutput)
        {
            DxgiOutput->Release();
            DxgiOutput = nullptr;
        }
        hr = DxgiAdapter->EnumOutputs(OutputCount, &DxgiOutput);
        if (DxgiOutput && (hr != DXGI_ERROR_NOT_FOUND))
        {
            DxgiOutput->GetDesc(&m_DesktopDesc);
            if (_wcsicmp(target_display, m_DesktopDesc.DeviceName) == 0)
            {
                // found target display
                break;
            }
        }
    }
    DxgiAdapter->Release();
    DxgiAdapter = nullptr;
    if (FAILED(hr))
    {
        TRACE(_T("cannot found matched target display hr: 0x%x\n"), hr);
        return hr;
    }

    // QI for Output 1
    IDXGIOutput1* DxgiOutput1 = nullptr;
    hr = DxgiOutput->QueryInterface(__uuidof(DxgiOutput1), reinterpret_cast<void**>(&DxgiOutput1));
    DxgiOutput->Release();
    DxgiOutput = nullptr;
    if (FAILED(hr))
    {
        TRACE(_T("Failed to QI for DxgiOutput1 hr: 0x%x\n"), hr);
        return hr;
    }

    // Create desktop duplication
    hr = DxgiOutput1->DuplicateOutput(m_Device, &m_DeskDupl);
    DxgiOutput1->Release();
    DxgiOutput1 = nullptr;
    if (FAILED(hr))
    {
        TRACE(_T("Failed to get duplicate output hr: 0x%x\n"), hr);
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
        {
            TRACE(_T("There is already the maximum number of applications using the Desktop Duplication API running, please close one of those applications and then try again.\n"));
        }
        return hr;
    }

    m_DeskDupl->GetDesc(&m_DuplicationDesc);

    m_width = m_DuplicationDesc.ModeDesc.Width;
    m_height = m_DuplicationDesc.ModeDesc.Height;
    m_bytepixel = get_bytepixel(m_DuplicationDesc.ModeDesc.Format);
    m_frame_buffer_len = m_width * m_height * m_bytepixel;
    m_frame_buffer = new uint8_t[m_frame_buffer_len];

    m_fps = fps;

    TRACE(_T("initialize success\n"));
    TRACE(_T("output description\n"));
    TRACE(_T("\tdevice name: %ws\n"), m_DesktopDesc.DeviceName);
    TRACE(_T("\ttcoordinates: (%d x %d) - (%d x %d)\n"), m_DesktopDesc.DesktopCoordinates.left, m_DesktopDesc.DesktopCoordinates.top,
        m_DesktopDesc.DesktopCoordinates.right, m_DesktopDesc.DesktopCoordinates.bottom);
    TRACE(_T("duplication description\n"));
    TRACE(_T("\tsize: %d x %d\n"), m_width, m_height);
    TRACE(_T("\tbyte pixel: %d\n"), m_bytepixel);
    TRACE(_T("\tformat: %hs\n"), get_duplicate_format(m_DuplicationDesc.ModeDesc.Format));
    TRACE(_T("\trotation: %hs\n"), get_duplicate_rotation(m_DuplicationDesc.Rotation));

    return hr;
}

void Duplicator::desktop_duplication_thread()
{
    HRESULT hr;

    std::chrono::high_resolution_clock::time_point t_start, t_done;
    std::chrono::microseconds t_spend;
    int64_t frame_us = (1 * 1000 * 1000) / (m_fps * 2); // doubling frames per seconds

    IDXGIResource* DesktopResource = NULL;
    ID3D11Texture2D* pAcquiredDesktopImage = NULL;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;

    while (m_capture_running)
    {
        t_start = std::chrono::high_resolution_clock::now();

        hr = m_DeskDupl->ReleaseFrame();
        if (FAILED(hr))
        {
            TRACE(_T("Failed to release frame hr: 0x%x\n"), hr);
        }

        // Get new frame
        hr = m_DeskDupl->AcquireNextFrame(INFINITE, &FrameInfo, &DesktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        {
            TRACE(_T("AcquireNextFrame timed out\n"));
            break;
        }

        if (FAILED(hr))
        {
            TRACE(_T("Failed to acquire next frame hr: 0x%x\n"), hr);
            break;
        }

        // If still holding old frame, destroy it
        if (pAcquiredDesktopImage)
        {
            pAcquiredDesktopImage->Release();
            pAcquiredDesktopImage = nullptr;
        }

        // QI for IDXGIResource
        hr = DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pAcquiredDesktopImage));
        DesktopResource->Release();
        DesktopResource = nullptr;
        if (FAILED(hr))
        {
            TRACE(_T("Failed to QI for ID3D11Texture2D from acquired IDXGIResource hr: 0x%x\n"), hr);
            break;
        }

        D3D11_TEXTURE2D_DESC desc;
        pAcquiredDesktopImage->GetDesc(&desc);

        D3D11_TEXTURE2D_DESC desc2;
        desc2.Width = desc.Width;
        desc2.Height = desc.Height;
        desc2.MipLevels = 1;
        desc2.ArraySize = 1;
        desc2.Format = desc.Format;
        desc2.SampleDesc.Count = 1;
        desc2.SampleDesc.Quality = 0;
        desc2.Usage = D3D11_USAGE_STAGING;
        desc2.BindFlags = 0;
        desc2.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc2.MiscFlags = 0;

        ID3D11Texture2D* texture;
        hr = m_Device->CreateTexture2D(&desc2, nullptr, &texture);
        if (FAILED(hr))
        {
            TRACE(_T("Failed to create staging texture hr: 0x%x\n"), hr);
            break;
        }

        // copy the texture to a staging resource
        m_Context->CopyResource(texture, pAcquiredDesktopImage);
        pAcquiredDesktopImage->Release();
        pAcquiredDesktopImage = nullptr;

        // now, map the staging resource
        D3D11_MAPPED_SUBRESOURCE mapInfo;
        UINT subresource = D3D11CalcSubresource(0, 0, 0);
        hr = m_Context->Map(texture, subresource, D3D11_MAP_READ, 0, &mapInfo);
        if (FAILED(hr))
        {
            TRACE(_T("Failed to map texture hr: 0x%x\n"), hr);
            break;
        }

        m_mutex.lock();
        if (m_frame_buffer)
        {
            // copy texture to bitmap buffer
            unsigned char* dst_buffer = m_frame_buffer;
            unsigned char* src_buffer = reinterpret_cast<unsigned char*>(mapInfo.pData);
            for (int row = 0; row < m_height; row++)
            {
                memcpy(dst_buffer, src_buffer, ((uint64_t)m_width * m_bytepixel));
                dst_buffer += ((uint64_t)m_width * m_bytepixel);
                src_buffer += mapInfo.RowPitch;
            }
        }
        m_mutex.unlock();

        m_Context->Unmap(texture, subresource);

        texture->Release();
        texture = nullptr;

        t_done = std::chrono::high_resolution_clock::now();
        t_spend = std::chrono::duration_cast<std::chrono::microseconds>(t_done - t_start);
        if (t_spend.count() < frame_us)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(frame_us - t_spend.count()));
        }
    }
}

void Duplicator::start_duplicate()
{
    m_capture_thread = std::move(std::thread([=]() {
        m_capture_running = true;
        desktop_duplication_thread();
        }));
}

void Duplicator::stop_duplicate()
{
    if (m_capture_running)
    {
        m_capture_running = false;
        if (m_capture_thread.joinable())
        {
            m_capture_thread.join();
        }
    }
}

int32_t Duplicator::get_frame_data(uint8_t* buffer)
{
    if (!m_frame_buffer)
    {
        return -1;
    }

    m_mutex.lock();
    CopyMemory(buffer, m_frame_buffer, m_frame_buffer_len);
    m_mutex.unlock();

    return 0;
}

int32_t Duplicator::get_frame_data_yuv420(uint8_t* buffer)
{
    if (!m_frame_buffer)
    {
        return -1;
    }

    int32_t i, j;
    int32_t r, g, b;
    int32_t y, u, v;
    int32_t sum;
    int32_t si0, si1, sj0, sj1;

    uint8_t *pYUV = new uint8_t[m_frame_buffer_len];

    m_mutex.lock();

    for (j = 0; j < m_height; j++) {
        for (i = 0; i < m_width; i++) {
            b = m_frame_buffer[(i + j * m_width) * 3];
            g = m_frame_buffer[(i + j * m_width) * 3 + 1];
            r = m_frame_buffer[(i + j * m_width) * 3 + 2];

            y = 0.299 * r + 0.587 * g + 0.114 * b;
            u = -0.169 * r - 0.331 * g + 0.500 * b + 128;
            v = 0.500 * r - 0.419 * g - 0.081 * b + 128;
            if (y < 0) { y = 0; }
            else if (y > 255) { y = 255; }
            if (u < 0) { u = 0; }
            else if (u > 255) { u = 255; }
            if (v < 0) { v = 0; }
            else if (v > 255) { v = 255; }

            pYUV[(i + j * m_width) * 3] = y;
            pYUV[(i + j * m_width) * 3 + 1] = u;
            pYUV[(i + j * m_width) * 3 + 2] = v;
        }
    }

    for (j = 0; j < m_height; j++) {
        for (i = 0; i < m_width; i++) {
            buffer[(i + j * m_width) * 3] = pYUV[(i + j * m_width) * 3];
        }
    }

    for (j = 0; j < m_height; j += 2) {
        sj0 = j;
        sj1 = (j + 1 < m_height) ? j + 1 : j;

        for (i = 0; i < m_width; i += 2) {
            si0 = i;
            si1 = (i + 1 < m_width) ? i + 1 : i;

            sum = pYUV[(si0 + sj0 * m_width) * 3 + 1];
            sum += pYUV[(si1 + sj0 * m_width) * 3 + 1];
            sum += pYUV[(si0 + sj1 * m_width) * 3 + 1];
            sum += pYUV[(si1 + sj1 * m_width) * 3 + 1];

            sum = sum / 4;

            buffer[(si0 + sj0 * m_width) * 3 + 1] = sum;
            buffer[(si1 + sj0 * m_width) * 3 + 1] = sum;
            buffer[(si0 + sj1 * m_width) * 3 + 1] = sum;
            buffer[(si1 + sj1 * m_width) * 3 + 1] = sum;

            sum = pYUV[(si0 + sj0 * m_width) * 3 + 2];
            sum += pYUV[(si1 + sj0 * m_width) * 3 + 2];
            sum += pYUV[(si0 + sj1 * m_width) * 3 + 2];
            sum += pYUV[(si1 + sj1 * m_width) * 3 + 2];

            sum = sum / 4;

            buffer[(si0 + sj0 * m_width) * 3 + 2] = sum;
            buffer[(si1 + sj0 * m_width) * 3 + 2] = sum;
            buffer[(si0 + sj1 * m_width) * 3 + 2] = sum;
            buffer[(si1 + sj1 * m_width) * 3 + 2] = sum;
        }
    }

    m_mutex.unlock();

    delete[] pYUV;

    return 0;
}

int32_t Duplicator::get_bytepixel(DXGI_FORMAT format)
{
    unsigned int bpp = 0;

    switch (static_cast<int>(format))
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        bpp = 16;
        break;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        bpp = 12;
        break;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_Y416:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
        bpp = 8;
        break;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_Y410:
    case DXGI_FORMAT_YUY2:
    //case XBOX_DXGI_FORMAT_R10G10B10_7E3_A2_FLOAT:
    //case XBOX_DXGI_FORMAT_R10G10B10_6E4_A2_FLOAT:
    //case XBOX_DXGI_FORMAT_R10G10B10_SNORM_A2_UNORM:
        bpp = 4;
        break;

    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
    //case XBOX_DXGI_FORMAT_D16_UNORM_S8_UINT:
    //case XBOX_DXGI_FORMAT_R16_UNORM_X8_TYPELESS:
    //case XBOX_DXGI_FORMAT_X16_TYPELESS_G8_UINT:
    //case WIN10_DXGI_FORMAT_V408:
        bpp = 4;
        break;

    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_A8P8:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
    //case WIN10_DXGI_FORMAT_P208:
    //case WIN10_DXGI_FORMAT_V208:
        bpp = 2;
        break;
/*
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_420_OPAQUE:
    case DXGI_FORMAT_NV11:
        return 12; 12 bits
*/
    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_AI44:
    case DXGI_FORMAT_IA44:
    case DXGI_FORMAT_P8:
    //case XBOX_DXGI_FORMAT_R4G4_UNORM:
        bpp = 1;
        break;
/*
    case DXGI_FORMAT_R1_UNORM:
        return 1; 1 bits

    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        return 4; 4 bits

    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return 8; 8 bits
*/
    }

    return bpp;
}

char* Duplicator::get_duplicate_rotation(DXGI_MODE_ROTATION rotation)
{
    switch (rotation)
    {
    case DXGI_MODE_ROTATION_UNSPECIFIED:
        return "DXGI_MODE_ROTATION_UNSPECIFIED";
    case DXGI_MODE_ROTATION_IDENTITY:
        return "DXGI_MODE_ROTATION_IDENTITY";
    case DXGI_MODE_ROTATION_ROTATE90:
        return "DXGI_MODE_ROTATION_ROTATE90";
    case DXGI_MODE_ROTATION_ROTATE180:
        return "DXGI_MODE_ROTATION_ROTATE180";
    case DXGI_MODE_ROTATION_ROTATE270:
        return "DXGI_MODE_ROTATION_ROTATE270";
    }

    return "unknown duplicate rotation";
}

char* Duplicator::get_duplicate_format(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_UNKNOWN:
        return "DXGI_FORMAT_UNKNOWN";
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return "DXGI_FORMAT_R32G32B32A32_TYPELESS";
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return "DXGI_FORMAT_R32G32B32A32_FLOAT";
    case DXGI_FORMAT_R32G32B32A32_UINT:
        return "DXGI_FORMAT_R32G32B32A32_UINT";
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return "DXGI_FORMAT_R32G32B32A32_SINT";
    case DXGI_FORMAT_R32G32B32_TYPELESS:
        return "DXGI_FORMAT_R32G32B32_TYPELESS";
    case DXGI_FORMAT_R32G32B32_FLOAT:
        return "DXGI_FORMAT_R32G32B32_FLOAT";
    case DXGI_FORMAT_R32G32B32_UINT:
        return "DXGI_FORMAT_R32G32B32_UINT";
    case DXGI_FORMAT_R32G32B32_SINT:
        return "DXGI_FORMAT_R32G32B32_SINT";
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return "DXGI_FORMAT_R16G16B16A16_TYPELESS";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return "DXGI_FORMAT_R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_UNORM:
        return "DXGI_FORMAT_R16G16B16A16_UNORM";
    case DXGI_FORMAT_R16G16B16A16_UINT:
        return "DXGI_FORMAT_R16G16B16A16_UINT";
    case DXGI_FORMAT_R16G16B16A16_SNORM:
        return "DXGI_FORMAT_R16G16B16A16_SNORM";
    case DXGI_FORMAT_R16G16B16A16_SINT:
        return "DXGI_FORMAT_R16G16B16A16_SINT";
    case DXGI_FORMAT_R32G32_TYPELESS:
        return "DXGI_FORMAT_R32G32_TYPELESS";
    case DXGI_FORMAT_R32G32_FLOAT:
        return "DXGI_FORMAT_R32G32_FLOAT";
    case DXGI_FORMAT_R32G32_UINT:
        return "DXGI_FORMAT_R32G32_UINT";
    case DXGI_FORMAT_R32G32_SINT:
        return "DXGI_FORMAT_R32G32_SINT";
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        return "DXGI_FORMAT_R32G8X24_TYPELESS";
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return "DXGI_FORMAT_D32_FLOAT_S8X24_UINT";
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        return "DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS";
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return "DXGI_FORMAT_X32_TYPELESS_G8X24_UINT";
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return "DXGI_FORMAT_R10G10B10A2_TYPELESS";
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return "DXGI_FORMAT_R10G10B10A2_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return "DXGI_FORMAT_R10G10B10A2_UINT";
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return "DXGI_FORMAT_R11G11B10_FLOAT";
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return "DXGI_FORMAT_R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return "DXGI_FORMAT_R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_UINT:
        return "DXGI_FORMAT_R8G8B8A8_UINT";
    case DXGI_FORMAT_R8G8B8A8_SNORM:
        return "DXGI_FORMAT_R8G8B8A8_SNORM";
    case DXGI_FORMAT_R8G8B8A8_SINT:
        return "DXGI_FORMAT_R8G8B8A8_SINT";
    case DXGI_FORMAT_R16G16_TYPELESS:
        return "DXGI_FORMAT_R16G16_TYPELESS";
    case DXGI_FORMAT_R16G16_FLOAT:
        return "DXGI_FORMAT_R16G16_FLOAT";
    case DXGI_FORMAT_R16G16_UNORM:
        return "DXGI_FORMAT_R16G16_UNORM";
    case DXGI_FORMAT_R16G16_UINT:
        return "DXGI_FORMAT_R16G16_UINT";
    case DXGI_FORMAT_R16G16_SNORM:
        return "DXGI_FORMAT_R16G16_SNORM";
    case DXGI_FORMAT_R16G16_SINT:
        return "DXGI_FORMAT_R16G16_SINT";
    case DXGI_FORMAT_R32_TYPELESS:
        return "DXGI_FORMAT_R32_TYPELESS";
    case DXGI_FORMAT_D32_FLOAT:
        return "DXGI_FORMAT_D32_FLOAT";
    case DXGI_FORMAT_R32_FLOAT:
        return "DXGI_FORMAT_R32_FLOAT";
    case DXGI_FORMAT_R32_UINT:
        return "DXGI_FORMAT_R32_UINT";
    case DXGI_FORMAT_R32_SINT:
        return "DXGI_FORMAT_R32_SINT";
    case DXGI_FORMAT_R24G8_TYPELESS:
        return "DXGI_FORMAT_R24G8_TYPELESS";
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return "DXGI_FORMAT_D24_UNORM_S8_UINT";
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        return "DXGI_FORMAT_R24_UNORM_X8_TYPELESS";
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return "DXGI_FORMAT_X24_TYPELESS_G8_UINT";
    case DXGI_FORMAT_R8G8_TYPELESS:
        return "DXGI_FORMAT_R8G8_TYPELESS";
    case DXGI_FORMAT_R8G8_UNORM:
        return "DXGI_FORMAT_R8G8_UNORM";
    case DXGI_FORMAT_R8G8_UINT:
        return "DXGI_FORMAT_R8G8_UINT";
    case DXGI_FORMAT_R8G8_SNORM:
        return "DXGI_FORMAT_R8G8_SNORM";
    case DXGI_FORMAT_R8G8_SINT:
        return "DXGI_FORMAT_R8G8_SINT";
    case DXGI_FORMAT_R16_TYPELESS:
        return "DXGI_FORMAT_R16_TYPELESS";
    case DXGI_FORMAT_R16_FLOAT:
        return "DXGI_FORMAT_R16_FLOAT";
    case DXGI_FORMAT_D16_UNORM:
        return "DXGI_FORMAT_D16_UNORM";
    case DXGI_FORMAT_R16_UNORM:
        return "DXGI_FORMAT_R16_UNORM";
    case DXGI_FORMAT_R16_UINT:
        return "DXGI_FORMAT_R16_UINT";
    case DXGI_FORMAT_R16_SNORM:
        return "DXGI_FORMAT_R16_SNORM";
    case DXGI_FORMAT_R16_SINT:
        return "DXGI_FORMAT_R16_SINT";
    case DXGI_FORMAT_R8_TYPELESS:
        return "DXGI_FORMAT_R8_TYPELESS";
    case DXGI_FORMAT_R8_UNORM:
        return "DXGI_FORMAT_R8_UNORM";
    case DXGI_FORMAT_R8_UINT:
        return "DXGI_FORMAT_R8_UINT";
    case DXGI_FORMAT_R8_SNORM:
        return "DXGI_FORMAT_R8_SNORM";
    case DXGI_FORMAT_R8_SINT:
        return "DXGI_FORMAT_R8_SINT";
    case DXGI_FORMAT_A8_UNORM:
        return "DXGI_FORMAT_A8_UNORM";
    case DXGI_FORMAT_R1_UNORM:
        return "DXGI_FORMAT_R1_UNORM";
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        return "DXGI_FORMAT_R9G9B9E5_SHAREDEXP";
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
        return "DXGI_FORMAT_R8G8_B8G8_UNORM";
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
        return "DXGI_FORMAT_G8R8_G8B8_UNORM";
    case DXGI_FORMAT_BC1_TYPELESS:
        return "DXGI_FORMAT_BC1_TYPELESS";
    case DXGI_FORMAT_BC1_UNORM:
        return "DXGI_FORMAT_BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return "DXGI_FORMAT_BC1_UNORM_SRGB";
    case DXGI_FORMAT_BC2_TYPELESS:
        return "DXGI_FORMAT_BC2_TYPELESS";
    case DXGI_FORMAT_BC2_UNORM:
        return "DXGI_FORMAT_BC2_UNORM";
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        return "DXGI_FORMAT_BC2_UNORM_SRGB";
    case DXGI_FORMAT_BC3_TYPELESS:
        return "DXGI_FORMAT_BC3_TYPELESS";
    case DXGI_FORMAT_BC3_UNORM:
        return "DXGI_FORMAT_BC3_UNORM";
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return "DXGI_FORMAT_BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_TYPELESS:
        return "DXGI_FORMAT_BC4_TYPELESS";
    case DXGI_FORMAT_BC4_UNORM:
        return "DXGI_FORMAT_BC4_UNORM";
    case DXGI_FORMAT_BC4_SNORM:
        return "DXGI_FORMAT_BC4_SNORM";
    case DXGI_FORMAT_BC5_TYPELESS:
        return "DXGI_FORMAT_BC5_TYPELESS";
    case DXGI_FORMAT_BC5_UNORM:
        return "DXGI_FORMAT_BC5_UNORM";
    case DXGI_FORMAT_BC5_SNORM:
        return "DXGI_FORMAT_BC5_SNORM";
    case DXGI_FORMAT_B5G6R5_UNORM:
        return "DXGI_FORMAT_B5G6R5_UNORM";
    case DXGI_FORMAT_B5G5R5A1_UNORM:
        return "DXGI_FORMAT_B5G5R5A1_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return "DXGI_FORMAT_B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return "DXGI_FORMAT_B8G8R8X8_UNORM";
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return "DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM";
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return "DXGI_FORMAT_B8G8R8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return "DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return "DXGI_FORMAT_B8G8R8X8_TYPELESS";
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return "DXGI_FORMAT_B8G8R8X8_UNORM_SRGB";
    case DXGI_FORMAT_BC6H_TYPELESS:
        return "DXGI_FORMAT_BC6H_TYPELESS";
    case DXGI_FORMAT_BC6H_UF16:
        return "DXGI_FORMAT_BC6H_UF16";
    case DXGI_FORMAT_BC6H_SF16:
        return "DXGI_FORMAT_BC6H_SF16";
    case DXGI_FORMAT_BC7_TYPELESS:
        return "DXGI_FORMAT_BC7_TYPELESS";
    case DXGI_FORMAT_BC7_UNORM:
        return "DXGI_FORMAT_BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return "DXGI_FORMAT_BC7_UNORM_SRGB";
    case DXGI_FORMAT_AYUV:
        return "DXGI_FORMAT_AYUV";
    case DXGI_FORMAT_Y410:
        return "DXGI_FORMAT_Y410";
    case DXGI_FORMAT_Y416:
        return "DXGI_FORMAT_Y416";
    case DXGI_FORMAT_NV12:
        return "DXGI_FORMAT_NV12";
    case DXGI_FORMAT_P010:
        return "DXGI_FORMAT_P010";
    case DXGI_FORMAT_P016:
        return "DXGI_FORMAT_P016";
    case DXGI_FORMAT_420_OPAQUE:
        return "DXGI_FORMAT_420_OPAQUE";
    case DXGI_FORMAT_YUY2:
        return "DXGI_FORMAT_YUY2";
    case DXGI_FORMAT_Y210:
        return "DXGI_FORMAT_Y210";
    case DXGI_FORMAT_Y216:
        return "DXGI_FORMAT_Y216";
    case DXGI_FORMAT_NV11:
        return "DXGI_FORMAT_NV11";
    case DXGI_FORMAT_AI44:
        return "DXGI_FORMAT_AI44";
    case DXGI_FORMAT_IA44:
        return "DXGI_FORMAT_IA44";
    case DXGI_FORMAT_P8:
        return "DXGI_FORMAT_P8";
    case DXGI_FORMAT_A8P8:
        return "DXGI_FORMAT_A8P8";
    case DXGI_FORMAT_B4G4R4A4_UNORM:
        return "DXGI_FORMAT_B4G4R4A4_UNORM";
    case DXGI_FORMAT_P208:
        return "DXGI_FORMAT_P208";
    case DXGI_FORMAT_V208:
        return "DXGI_FORMAT_V208";
    case DXGI_FORMAT_V408:
        return "DXGI_FORMAT_V408";
    case DXGI_FORMAT_FORCE_UINT:
        return "DXGI_FORMAT_FORCE_UINT";
    }

    return "unknown duplication format";
}
