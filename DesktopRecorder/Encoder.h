#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class Encoder
{
public:
	Encoder();
	~Encoder();

	void set_width(uint32_t width) { m_width = width; }
	void set_height(uint32_t height) { m_height = height; }
	void set_bytepixel(uint32_t bytepixel) { m_bytepixel = bytepixel; }
	void set_fps(uint32_t fps) { m_fps = fps; }
	void set_bitrate(uint32_t bitrate) { m_bitrate = bitrate; }

	void output_thread();
	int32_t initialize();
	int32_t encode_frame(uint8_t* buffer);
	int32_t output_open(const char* filename);
	int32_t output_close();

private:
	AVFormatContext* m_output_context;
	AVStream* m_video_stream;
	AVCodecContext* m_codec_context;
	SwsContext* m_swsctx;
	AVFrame* m_frame;
	int64_t m_frame_count;

	int32_t m_width;
	int32_t m_height;
	int32_t m_bytepixel;
	int32_t m_fps;
	int32_t m_bitrate;
	int32_t m_frame_length;

	bool m_output_running;
	std::thread m_output_thread;
};

