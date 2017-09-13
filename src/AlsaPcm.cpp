/*
 *  Alsa pcm device wrapper
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (C) 2016 EPAM Systems Inc.
 */

#include "AlsaPcm.hpp"

#include <xen/io/sndif.h>

using std::string;
using std::thread;
using std::to_string;

using SoundItf::PcmParams;
using SoundItf::SoundException;
using SoundItf::StreamType;

namespace Alsa {

/*******************************************************************************
 * AlsaPcm
 ******************************************************************************/

AlsaPcm::AlsaPcm(StreamType type, const std::string& deviceName) :
	mHandle(nullptr),
	mAsyncHandle(nullptr),
	mDeviceName(deviceName),
	mType(type),
	mBufferTime(500000),
	mPeriodTime(100000),
	mLog("AlsaPcm")
{
	if (mDeviceName.empty())
	{
		mDeviceName = "default";
	}

	LOG(mLog, DEBUG) << "Create pcm device: " << mDeviceName;
}

AlsaPcm::~AlsaPcm()
{
	LOG(mLog, DEBUG) << "Delete pcm device: " << mDeviceName;

	close();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

void AlsaPcm::open(const PcmParams& params)
{
	try
	{
		DLOG(mLog, DEBUG) << "Open pcm device: " << mDeviceName
						  << ", format: " << static_cast<int>(params.format)
						  << ", rate: " << params.rate
						  << ", channels: " << static_cast<int>(params.numChannels);

		snd_pcm_stream_t streamType = mType == StreamType::PLAYBACK ?
				SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE;

		int ret = 0;

		if ((ret = snd_pcm_open(&mHandle, mDeviceName.c_str(),
							    streamType, 0)) < 0)
		{
			throw SoundException("Can't open audio device " + mDeviceName,
								 ret);
		}

		setHwParams(params);
		setSwParams();

		snd_output_t *output;

		snd_output_stdio_attach(&output, stdout, 0);

		snd_pcm_dump(mHandle, output);
/*
		if ((ret = snd_async_add_pcm_handler(&mAsyncHandle, mHandle, sAsyncCallback, this)) < 0)
		{
			throw SoundException("Can't add async handler " + mDeviceName + snd_strerror(ret), ret);
		}

		if ((ret = snd_pcm_start(mHandle)) < 0)
		{
			throw SoundException("Can't start async " + mDeviceName, ret);
		}
*/

		startPollFds();
/*
		if ((ret = snd_pcm_prepare(mHandle)) < 0)
		{
			throw SoundException(
					"Can't prepare audio interface for use", ret);
		}
		*/
	}
	catch(const SoundException& e)
	{
		close();

		throw;
	}
}

void AlsaPcm::close()
{

	stopPollFds();

	if (mAsyncHandle)
	{
		snd_async_del_handler(mAsyncHandle);

		mAsyncHandle = nullptr;
	}

	if (mHandle)
	{
		DLOG(mLog, DEBUG) << "Close pcm device: " << mDeviceName;

		snd_pcm_drain(mHandle);
		snd_pcm_close(mHandle);

		mHandle = nullptr;
	}

	stopPollFds();
}

void AlsaPcm::read(uint8_t* buffer, size_t size)
{
	DLOG(mLog, DEBUG) << "Read from pcm device: " << mDeviceName
					  << ", size: " << size;

	if (!mHandle)
	{
		throw SoundException("Alsa device is not opened: " +
							 mDeviceName + ". Error: " +
							 snd_strerror(-EFAULT), -EFAULT);
	}

	auto numFrames = snd_pcm_bytes_to_frames(mHandle, size);

	while(numFrames > 0)
	{
		if (auto status = snd_pcm_readi(mHandle, buffer, numFrames))
		{
			if (status == -EPIPE)
			{
				LOG(mLog, WARNING) << "Device: " << mDeviceName
								   << ", message: " << snd_strerror(status);

				snd_pcm_prepare(mHandle);
			}
			else if (status < 0)
			{
				throw SoundException("Read from audio interface failed: " +
									 mDeviceName + ". Error: " +
									 snd_strerror(status), status);
			}
			else
			{
				numFrames -= status;
				buffer = &buffer[snd_pcm_frames_to_bytes(mHandle, status)];
			}
		}
	}
}

void AlsaPcm::write(uint8_t* buffer, size_t size)
{
	if (!mHandle)
	{
		throw SoundException("Alsa device is not opened: " +
							 mDeviceName + ". Error: " +
							 snd_strerror(-EFAULT), -EFAULT);
	}

	auto numFrames = snd_pcm_bytes_to_frames(mHandle, size);

	while(numFrames > 0)
	{
#if 0
		int err;

		if ((err = snd_pcm_wait(mHandle, 1000)) < 0)
		{
			throw SoundException("Failed to pcm wait: " +
								 mDeviceName + ". Error: " +
								 snd_strerror(err), err);
		}

		int frames;

		if ((frames = snd_pcm_avail_update(mHandle)) < 0)
		{
			throw SoundException("Failed to pcm avail update: " +
								 mDeviceName + ". Error: " +
								 snd_strerror(frames), frames);
		}

		LOG(mLog, DEBUG) << "Avail frames: " << frames;
#endif
		if (auto status = snd_pcm_writei(mHandle, buffer, numFrames))
		{
			DLOG(mLog, DEBUG) << "Write to pcm device: " << mDeviceName
							  << ", size: " << status;


			if (status == -EPIPE)
			{
				LOG(mLog, WARNING) << "Device: " << mDeviceName
								   << ", message: " << snd_strerror(status);

				snd_pcm_prepare(mHandle);
			}
			else if (status < 0)
			{
				throw SoundException("Write to audio interface failed: " +
									 mDeviceName + ". Error: " +
									 snd_strerror(status), status);
			}
			else
			{
				numFrames -= status;
				buffer = &buffer[snd_pcm_frames_to_bytes(mHandle, status)];
			}
		}
	}
#if 0
	if (snd_pcm_state(mHandle) == SND_PCM_STATE_RUNNING)
	{
		waitForPoll();
	}
#endif
	snd_pcm_status_t *status;

	snd_pcm_status_alloca(&status);

	if (snd_pcm_status(mHandle, status) < 0)
	{
		LOG(mLog, ERROR) << "Can't get status";
	}

	snd_timestamp_t time = {};

	snd_pcm_status_get_tstamp(status, &time);

	LOG(mLog, DEBUG) << "Timestamp: " << time.tv_sec << "." << time.tv_usec;

}

/*******************************************************************************
 * Private
 ******************************************************************************/
void AlsaPcm::sAsyncCallback(snd_async_handler_t *handler)
{
	void *data = snd_async_handler_get_callback_private(handler);

	static_cast<AlsaPcm*>(data)->asyncCallback();
}

void AlsaPcm::asyncCallback()
{
	LOG(mLog, DEBUG) << "====== Async callback";
}

void AlsaPcm::setHwParams(const PcmParams& params)
{
	snd_pcm_hw_params_t *hwParams = nullptr;

	try
	{
		int ret = 0;

		if ((ret = snd_pcm_hw_params_malloc(&hwParams)) < 0)
		{
			throw SoundException("Can't allocate hw params " + mDeviceName,
								 ret);
		}

		if ((ret = snd_pcm_hw_params_any(mHandle, hwParams)) < 0)
		{
			throw SoundException("Can't allocate hw params " + mDeviceName,
								 ret);
		}

		if ((ret = snd_pcm_hw_params_set_access(mHandle, hwParams,
				SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
		{
			throw SoundException("Can't set access " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_hw_params_set_format(mHandle, hwParams,
				convertPcmFormat(params.format))) < 0)
		{
			throw SoundException("Can't set format " + mDeviceName, ret);
		}

		unsigned int rate = params.rate;

		if ((ret = snd_pcm_hw_params_set_rate_near(mHandle, hwParams,
												   &rate, 0)) < 0)
		{
			throw SoundException("Can't set rate " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_hw_params_set_channels(mHandle, hwParams,
												  params.numChannels)) < 0)
		{
			throw SoundException("Can't set channels " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_hw_params_set_buffer_time_near(mHandle, hwParams, &mBufferTime, 0)) < 0)
		{
			throw SoundException("Can't set buffer time " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_hw_params_set_period_time_near(mHandle, hwParams, &mPeriodTime, 0)) < 0)
		{
			throw SoundException("Can't set period time " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_hw_params(mHandle, hwParams)) < 0)
		{
			throw SoundException("Can't set hwParams " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_hw_params_get_period_size(hwParams, &mPeriodSize, 0)) < 0)
		{
			throw SoundException("Can't get period size " + mDeviceName + " " + snd_strerror(ret), ret);
		}

		LOG(mLog, DEBUG) << "Period size: " << mPeriodSize;

		if ((ret = snd_pcm_hw_params_get_buffer_size(hwParams, &mBufferSize)) < 0)
		{
			throw SoundException("Can't get buffer size " + mDeviceName + " " + snd_strerror(ret), ret);
		}

		LOG(mLog, DEBUG) << "Buffer size: " << mBufferSize;
	}
	catch(const SoundException& e)
	{
		if (hwParams)
		{
			snd_pcm_hw_params_free(hwParams);
		}

		throw;
	}
}

void AlsaPcm::setSwParams()
{
	snd_pcm_sw_params_t *swParams = nullptr;

	try
	{
		int ret = 0;

		if ((ret = snd_pcm_sw_params_malloc(&swParams)) < 0)
		{
			throw SoundException("Can't allocate sw params " + mDeviceName,
								 ret);
		}

		if ((ret = snd_pcm_sw_params_current(mHandle, swParams)) < 0)
		{
			throw SoundException("Can't get swParams " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_sw_params_set_start_threshold(mHandle, swParams, (mBufferSize / mPeriodSize) * mPeriodSize)) < 0)
		{
			throw SoundException("Can't set start threshold " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_sw_params_set_avail_min(mHandle, swParams, mBufferSize)) < 0)
		{
			throw SoundException("Can't set avail min " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_sw_params_set_period_event(mHandle, swParams, 1)) < 0)
		{
			throw SoundException("Can't period event " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_sw_params_set_tstamp_mode(mHandle, swParams, SND_PCM_TSTAMP_ENABLE)) < 0)
		{
			throw SoundException("Can't set ts mode " + mDeviceName, ret);
		}

		if ((ret = snd_pcm_sw_params(mHandle, swParams)) < 0)
		{
			throw SoundException("Can't set swParams " + mDeviceName, ret);
		}
	}
	catch(const SoundException& e)
	{
		if (swParams)
		{
			snd_pcm_sw_params_free(swParams);
		}

		throw;
	}
}

void AlsaPcm::startPollFds()
{
	int ret = 0;

	if ((ret = snd_pcm_poll_descriptors_count(mHandle)) < 0)
	{
		throw SoundException("Can't get poll fds count " + mDeviceName, ret);
	}

	mPollFds.resize(ret);

	LOG(mLog, DEBUG) << "Num descriptors: " << ret;

	if ((ret = snd_pcm_poll_descriptors(mHandle, mPollFds.data(), mPollFds.size())) < 0)
	{
		throw SoundException("Can't get poll fds " + mDeviceName, ret);
	}

	mTerminate = false;

	mThread = thread(&AlsaPcm::handlePollFds, this);

	LOG(mLog, DEBUG) << "Fds: " << mPollFds[0].events;
}

void AlsaPcm::stopPollFds()
{
}

void AlsaPcm::handlePollFds()
{
	while(!mTerminate)
	{
		unsigned short revents;

		auto ret = poll(mPollFds.data(), mPollFds.size(), -1);

		ret = snd_pcm_poll_descriptors_revents(mHandle, mPollFds.data(), mPollFds.size(), &revents);

		if (revents & POLLERR)
		{
			LOG(mLog, ERROR) << "==== Poll error " << revents;

			return;
		}

		if (revents & POLLOUT && snd_pcm_state(mHandle) == SND_PCM_STATE_RUNNING)
		{
			/*
			int frames;

			if ((frames = snd_pcm_avail_update(mHandle)) < 0)
			{
				return;
			}*/

			//LOG(mLog, DEBUG) << "==== Poll event " << time.tv_sec << "." << time.tv_nsec;

		}
	}
}

int AlsaPcm::waitForPoll()
{
	unsigned short revents;
	while (1)
	{
		poll(mPollFds.data(), mPollFds.size(), -1);
		snd_pcm_poll_descriptors_revents(mHandle, mPollFds.data(), mPollFds.size(), &revents);

		if (revents & POLLERR)
			return -EIO;

		if (revents & POLLOUT)
			return 0;
	}
}

AlsaPcm::PcmFormat AlsaPcm::sPcmFormat[] =
{
	{XENSND_PCM_FORMAT_U8,                 SND_PCM_FORMAT_U8 },
	{XENSND_PCM_FORMAT_S8,                 SND_PCM_FORMAT_S8 },
	{XENSND_PCM_FORMAT_U16_LE,             SND_PCM_FORMAT_U16_LE },
	{XENSND_PCM_FORMAT_U16_BE,             SND_PCM_FORMAT_U16_BE },
	{XENSND_PCM_FORMAT_S16_LE,             SND_PCM_FORMAT_S16_LE },
	{XENSND_PCM_FORMAT_S16_BE,             SND_PCM_FORMAT_S16_BE },
	{XENSND_PCM_FORMAT_U24_LE,             SND_PCM_FORMAT_U24_LE },
	{XENSND_PCM_FORMAT_U24_BE,             SND_PCM_FORMAT_U24_BE },
	{XENSND_PCM_FORMAT_S24_LE,             SND_PCM_FORMAT_S24_LE },
	{XENSND_PCM_FORMAT_S24_BE,             SND_PCM_FORMAT_S24_BE },
	{XENSND_PCM_FORMAT_U32_LE,             SND_PCM_FORMAT_U32_LE },
	{XENSND_PCM_FORMAT_U32_BE,             SND_PCM_FORMAT_U32_BE },
	{XENSND_PCM_FORMAT_S32_LE,             SND_PCM_FORMAT_S32_LE },
	{XENSND_PCM_FORMAT_S32_BE,             SND_PCM_FORMAT_S32_BE },
	{XENSND_PCM_FORMAT_A_LAW,              SND_PCM_FORMAT_A_LAW },
	{XENSND_PCM_FORMAT_MU_LAW,             SND_PCM_FORMAT_MU_LAW },
	{XENSND_PCM_FORMAT_F32_LE,             SND_PCM_FORMAT_FLOAT_LE },
	{XENSND_PCM_FORMAT_F32_BE,             SND_PCM_FORMAT_FLOAT_BE },
	{XENSND_PCM_FORMAT_F64_LE,             SND_PCM_FORMAT_FLOAT64_LE },
	{XENSND_PCM_FORMAT_F64_BE,             SND_PCM_FORMAT_FLOAT64_BE },
	{XENSND_PCM_FORMAT_IEC958_SUBFRAME_LE, SND_PCM_FORMAT_IEC958_SUBFRAME_LE },
	{XENSND_PCM_FORMAT_IEC958_SUBFRAME_BE, SND_PCM_FORMAT_IEC958_SUBFRAME_BE },
	{XENSND_PCM_FORMAT_IMA_ADPCM,          SND_PCM_FORMAT_IMA_ADPCM },
	{XENSND_PCM_FORMAT_MPEG,               SND_PCM_FORMAT_MPEG },
	{XENSND_PCM_FORMAT_GSM,                SND_PCM_FORMAT_GSM },
};

snd_pcm_format_t AlsaPcm::convertPcmFormat(uint8_t format)
{
	for (auto value : sPcmFormat)
	{
		if (value.sndif == format)
		{
			return value.alsa;
		}
	}

	throw SoundException("Can't convert format", -EINVAL);
}

}
