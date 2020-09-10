#pragma once

class Duplicator
{
public:
    Duplicator();
	~Duplicator();

    HRESULT initialize(const wchar_t* target_display, int32_t fps);
    int32_t get_width() { return m_width; }
    int32_t get_height() { return m_height; }
    int32_t get_bytepixel() { return m_bytepixel; }
    int32_t get_frame_buffer_length() { return m_frame_buffer_len; }
    int32_t get_frame_data(uint8_t *buffer);
    int32_t get_frame_data_yuv420(uint8_t* buffer);

    void desktop_duplication_thread();
    void start_duplicate();
    void stop_duplicate();

protected:
    int get_bytepixel(DXGI_FORMAT format);
    char* get_duplicate_rotation(DXGI_MODE_ROTATION rotation);
    char* get_duplicate_format(DXGI_FORMAT format);

private:
    int32_t m_width;
    int32_t m_height;
    int32_t m_bytepixel;
    int32_t m_frame_buffer_len;
    uint8_t* m_frame_buffer;

    ID3D11Device* m_Device;
    ID3D11DeviceContext* m_Context;
    IDXGIOutputDuplication* m_DeskDupl;
    ID3D11Texture2D* m_AcquiredDesktopImage;
    DXGI_OUTPUT_DESC m_DesktopDesc;
    DXGI_OUTDUPL_DESC m_DuplicationDesc;

    int32_t m_fps;
    bool m_capture_running;
    std::mutex m_mutex;
    std::thread m_capture_thread;
};

