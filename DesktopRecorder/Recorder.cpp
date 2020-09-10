#include "pch.h"
#include "Recorder.h"

Recorder::Recorder()
{
	m_duplicator = nullptr;
	m_encoder = nullptr;
	m_record_running = false;
	m_frame_buffer = nullptr;

	m_fps = 30;
}

Recorder::~Recorder()
{
	stop_record();

	if (m_frame_buffer) delete[] m_frame_buffer;
}

void Recorder::record_thread()
{
	std::chrono::high_resolution_clock::time_point t_start, t_done;
	std::chrono::microseconds t_spend;
	int64_t frame_us = (1 * 1000 * 1000) / (m_fps + 5); // frames per seconds
	int64_t remain_us = 0;

	int64_t sum_remain = 0;
	int32_t count = 0;

	while (m_record_running)
	{
		t_start = std::chrono::high_resolution_clock::now();

		// get current desktop raw image
		m_duplicator->get_frame_data(m_frame_buffer);

		// encode frame
		m_encoder->encode_frame(m_frame_buffer);

		t_done = std::chrono::high_resolution_clock::now();
		t_spend = std::chrono::duration_cast<std::chrono::microseconds>(t_done - t_start);
		remain_us = frame_us - t_spend.count();
		sum_remain += (remain_us < 0 ? 0 : remain_us);
		count++;
		if (count % m_fps == 0) TRACE(_T("average remain us = %ld\n"), sum_remain / count);
		if (t_spend.count() < frame_us)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(frame_us - t_spend.count()));
		}
	}
}

void Recorder::start_record()
{
	int32_t ret = 0;

	if (m_record_running)
	{
		return;
	}

	do
	{
		// create and start duplicator
		m_duplicator = new Duplicator();
		if (!m_duplicator)
		{
			ret = -1;
			break;
		}

		HRESULT hr = m_duplicator->initialize(L"\\\\.\\DISPLAY1", m_fps);
		if (FAILED(hr))
		{
			ret = -1;
			break;
		}

		m_frame_buffer = new uint8_t[m_duplicator->get_frame_buffer_length()];
		if (!m_frame_buffer)
		{
			ret = -1;
			break;
		}

		m_duplicator->start_duplicate();

		// create and initialize encoder
		m_encoder = new Encoder();
		if (!m_encoder)
		{
			ret = -1;
			break;
		}

		m_encoder->set_width(m_duplicator->get_width());
		m_encoder->set_height(m_duplicator->get_height());
		m_encoder->set_bytepixel(m_duplicator->get_bytepixel());
		m_encoder->set_fps(m_fps);
		m_encoder->set_bitrate(4 * 1000 * 1000);

		ret = m_encoder->initialize();
		if (ret < 0)
		{
			break;
		}

		ret = m_encoder->output_open("output.mp4");
		if (ret < 0)
		{
			break;
		}
	} while (false);

	if (ret < 0)
	{
		if (m_duplicator)
		{
			delete m_duplicator;
			m_duplicator = nullptr;
		}

		if (m_encoder)
		{
			delete m_encoder;
			m_encoder = nullptr;
		}

		return;
	}

	// start record thread
	m_record_thread = std::move(std::thread([=]() {
		m_record_running = true;
		record_thread();
		}));

	return;
}

void Recorder::stop_record()
{
	// stop record thread
	if (m_record_running)
	{
		m_record_running = false;
		if (m_record_thread.joinable())
		{
			m_record_thread.join();
		}
	}

	if (m_encoder)
	{
		m_encoder->output_close();
		delete m_encoder;
		m_encoder = nullptr;
	}

	// stop and delete duplicator
	if (m_duplicator)
	{
		m_duplicator->stop_duplicate();
		delete m_duplicator;
		m_duplicator = NULL;
	}
}