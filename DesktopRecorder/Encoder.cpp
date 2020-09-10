#include "pch.h"
#include "Encoder.h"

#pragma comment(lib, "avdevice.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avfilter.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swresample.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avutil.lib")

#pragma warning(disable : 4996)
#pragma warning(disable : 26812)

//#define ENABLE_OUTPUT_THREAD

Encoder::Encoder() :
	m_output_context(nullptr),
	m_video_stream(nullptr),
	m_codec_context(nullptr),
	m_swsctx(nullptr),
	m_frame(nullptr),
	m_frame_count(0),
	m_width(0),
	m_height(0),
	m_bytepixel(0),
	m_fps(0),
	m_bitrate(0),
	m_frame_length(0),
	m_output_running(false)
{

}

Encoder::~Encoder()
{
	if (m_swsctx)
	{
		sws_freeContext(m_swsctx);
		m_swsctx = nullptr;
	}

	if (m_frame)
	{
		av_freep(&m_frame->data[0]);
		av_frame_free(&m_frame);
		m_frame = nullptr;
	}

	if (m_codec_context)
	{
		avcodec_close(m_codec_context);
		avcodec_free_context(&m_codec_context);
		m_codec_context = nullptr;
	}

	if (m_output_context)
	{
		avformat_free_context(m_output_context);
		m_output_context = nullptr;
	}
}

int32_t Encoder::initialize()
{
	int32_t ret = 0;
	AVCodec* codec = NULL;

	m_frame_length = m_width * m_height * m_bytepixel;
	if (m_frame_length == 0)
	{
		TRACE(_T("display size invalid\n"));
		return -1;
	}

	if (m_fps == 0)
	{
		TRACE(_T("fps invalid\n"));
		return -1;
	}

	if (m_bitrate == 0)
	{
		TRACE(_T("bitrate invalid\n"));
		return -1;
	}

	ret = avformat_alloc_output_context2(&m_output_context, nullptr, "h264", nullptr);
	if (ret < 0)
	{
		TRACE(_T("cannot allocate ouput context\n"));
		return -1;
	}

	//codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	codec = avcodec_find_encoder_by_name("libx264");
	if (!codec)
	{
		TRACE(_T("cannot found codec\n"));
		return -1;
	}

	m_video_stream = avformat_new_stream(m_output_context, codec);
	if (!m_video_stream)
	{
		TRACE(_T("cannot create new video stream\n"));
		return -1;
	}

	m_codec_context = avcodec_alloc_context3(codec);
	if (!m_codec_context)
	{
		TRACE(_T("Cannot allocate codec context\n"));
		return -1;
	}

	m_video_stream->codecpar->codec_id = codec->id;
	m_video_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
	m_video_stream->codecpar->width = m_width;
	m_video_stream->codecpar->height = m_height;
	m_video_stream->codecpar->format = AV_PIX_FMT_YUV420P;
	m_video_stream->codecpar->bit_rate = m_bitrate;
	m_video_stream->time_base = { 1, m_fps };

	avcodec_parameters_to_context(m_codec_context, m_video_stream->codecpar);
	//m_codec_context->bit_rate = m_bitrate;
	//m_codec_context->width = m_width;
	//m_codec_context->height = m_height;
	m_codec_context->time_base = { 1, m_fps };
	m_codec_context->gop_size = 30;
	m_codec_context->max_b_frames = 0;
	//m_codec_context->pix_fmt = AV_PIX_FMT_YUV420P;

	if (codec->id == AV_CODEC_ID_H264)
	{
		// https://trac.ffmpeg.org/wiki/Encode/H.264
		// available presets
		// ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, veryslow
		av_opt_set(m_codec_context->priv_data, "preset", "faster", 0);
		// available tune
		// film, animation, grain, stillimage, fastdecode, zerolatency, psnr, ssim
		av_opt_set(m_codec_context->priv_data, "tune", "zerolatency", 0);
		//av_opt_set(m_codec_context->priv_data, "profile", "high", 0);
	}

	if (m_output_context->oformat->flags & AVFMT_GLOBALHEADER)
	{
		m_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	avcodec_parameters_from_context(m_video_stream->codecpar, m_codec_context);

	ret = avcodec_open2(m_codec_context, codec, nullptr);
	if (ret < 0)
	{
		TRACE(_T("cannot open codec %d\n"), ret);
		return -1;
	}

	m_frame = av_frame_alloc();
	if (!m_frame)
	{
		TRACE(_T("cannot allocate video frame\n"));
		return -1;
	}

	m_frame->format = m_codec_context->pix_fmt;
	m_frame->width = m_width;
	m_frame->height = m_height;

	ret = av_image_alloc(m_frame->data, m_frame->linesize, 
		m_codec_context->width,  m_codec_context->height, m_codec_context->pix_fmt, 32);
	if (ret < 0) {
		TRACE(_T("cannot allocate raw picture buffer\n"));
		return -1;
	}

	m_swsctx = nullptr;
	m_swsctx = sws_getContext(m_codec_context->width, m_codec_context->height, AV_PIX_FMT_BGRA, 
		m_codec_context->width, m_codec_context->height, AV_PIX_FMT_YUV420P, 0, 0, 0, 0);
	if (m_swsctx == nullptr)
	{
		TRACE(_T("sws_getContext error\n"));
		return -1;
	}

	return 0;
}

int32_t Encoder::encode_frame(uint8_t* buffer)
{
	int ret = 0;
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;

	uint8_t* inData[1] = { buffer };
	int in_linesize[1] = { m_bytepixel * m_frame->width };
	/*
	m_swsctx = sws_getCachedContext(m_swsctx, 
		m_frame->width, m_frame->height, AV_PIX_FMT_BGRA,
		m_frame->width, m_frame->height, AV_PIX_FMT_YUV420P,
		0, 0, 0, 0);
	*/
	sws_scale(m_swsctx, inData, in_linesize, 0, m_frame->height, m_frame->data, m_frame->linesize);

	m_frame->pts = m_frame_count++;

	ret = avcodec_send_frame(m_codec_context, m_frame);
	if (ret < 0)
	{
		TRACE(_T("avcodec_send_frame error %d\n"), ret);
		return -1;
	}
#ifndef ENABLE_OUTPUT_THREAD
	while (ret >= 0)
	{
		ret = avcodec_receive_packet(m_codec_context, &pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		{
			break;
		}
		else if (ret < 0)
		{
			TRACE(_T("error during encoding %d\n"), ret);
			break;
		}

		// save frame to file - data : m_pkt->data, size : m_pkt->size
		av_interleaved_write_frame(m_output_context, &pkt);

		av_packet_unref(&pkt);
	}
#endif
	return ret;
}

void Encoder::output_thread()
{
	std::chrono::high_resolution_clock::time_point t_start, t_done;
	std::chrono::microseconds t_spend;
	int64_t frame_us = (1 * 1000 * 1000) / (m_fps * 2); // frames per seconds

	int ret = 0;
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;

	t_start = std::chrono::high_resolution_clock::now();

	while (m_output_running)
	{
		t_done = std::chrono::high_resolution_clock::now();
		t_spend = std::chrono::duration_cast<std::chrono::microseconds>(t_done - t_start);
		if (t_spend.count() < frame_us)
			std::this_thread::sleep_for(std::chrono::microseconds(frame_us - t_spend.count()));
		t_start = std::chrono::high_resolution_clock::now();

		ret = avcodec_receive_packet(m_codec_context, &pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		{
			continue;
		}
		else if (ret < 0)
		{
			TRACE(_T("avcodec_receive_packet error %d\n"), ret);
			break;
		}

		av_interleaved_write_frame(m_output_context, &pkt);
		av_packet_unref(&pkt);
	}
}

int32_t Encoder::output_open(const char* filename)
{
	int ret = 0;

	av_dump_format(m_output_context, 0, filename, 1);

	if (!(m_output_context->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open(&m_output_context->pb, filename, AVIO_FLAG_WRITE);
		if (ret < 0) {
			TRACE(_T("cannot open output file\n"));
			return -1;
		}
	}

	ret = avformat_write_header(m_output_context, nullptr);
	if (ret < 0)
	{
		return -1;
	}
#ifdef ENABLE_OUTPUT_THREAD
	m_output_thread = std::move(std::thread([=]() {
		m_output_running = true;
		output_thread();
		}));
#endif
	return 0;
}

int32_t Encoder::output_close()
{
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
#ifdef ENABLE_OUTPUT_THREAD
	if (m_output_running)
	{
		m_output_running = false;
		if (m_output_thread.joinable())
		{
			m_output_thread.join();
		}
	}
#endif
	for (;;)
	{
		avcodec_send_frame(m_codec_context, nullptr);
		if (avcodec_receive_packet(m_codec_context, &pkt) == 0)
		{
			av_interleaved_write_frame(m_output_context, &pkt);
			av_packet_unref(&pkt);
		}
		else
		{
			break;
		}
	}

	av_write_trailer(m_output_context);

	if (!(m_output_context->oformat->flags & AVFMT_NOFILE)) {
		int err = avio_close(m_output_context->pb);
		if (err < 0) {
			TRACE(_T("failed to close output file\n"));
		}
	}

	return 0;
}
