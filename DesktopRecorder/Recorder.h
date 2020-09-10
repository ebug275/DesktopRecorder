#pragma once

#include "Duplicator.h"
#include "Encoder.h"

class Recorder
{
public:
	Recorder();
	~Recorder();

	void record_thread();
	void start_record();
	void stop_record();

private:
	Duplicator* m_duplicator;
	Encoder* m_encoder;

	int32_t m_fps;
	bool m_record_running;
	uint8_t* m_frame_buffer;
	std::thread m_record_thread;
};

