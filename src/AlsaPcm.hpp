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

#ifndef SRC_ALSAPCM_HPP_
#define SRC_ALSAPCM_HPP_

#include <alsa/asoundlib.h>

#include <xen/be/Log.hpp>
#include <xen/be/Utils.hpp>

#include "SoundItf.hpp"

namespace Alsa {

/***************************************************************************//**
 * @defgroup alsa
 * Alsa related classes.
 ******************************************************************************/

/***************************************************************************//**
 * Provides alsa pcm functionality.
 * @ingroup alsa
 ******************************************************************************/
class AlsaPcm : public SoundItf::PcmDevice
{
public:
	/**
	 * @param type stream type
	 * @param name pcm device name
	 */
	explicit AlsaPcm(SoundItf::StreamType type,
					 const std::string& deviceName = "default");
	~AlsaPcm();

	/**
	 * Opens the pcm device.
	 * @param params pcm parameters
	 */
	void open(const SoundItf::PcmParams& params) override;

	/**
	 * Closes the pcm device.
	 */
	void close() override;

	/**
	 * Reads data from the pcm device.
	 * @param buffer buffer where to put data
	 * @param size   number of bytes to read
	 */
	void read(uint8_t* buffer, size_t size) override;

	/**
	 * Writes data to the pcm device.
	 * @param buffer buffer with data
	 * @param size   number of bytes to write
	 */
	void write(uint8_t* buffer, size_t size) override;

	/**
	 * Sets progress callback.
	 * @param cbk callback
	 */
	void setProgressCbk(SoundItf::ProgressCbk cbk) override
	{ mProgressCbk = cbk; }

private:

	struct PcmFormat
	{
		uint8_t sndif;
		snd_pcm_format_t alsa;
	};

	static PcmFormat sPcmFormat[];

	snd_pcm_t *mHandle;
	std::string mDeviceName;
	SoundItf::StreamType mType;
	XenBackend::Timer mTimer;
	XenBackend::Log mLog;

	SoundItf::ProgressCbk mProgressCbk;
	unsigned int mRate;

	void setHwParams(const SoundItf::PcmParams& params);
	void setSwParams();
	void getTimeStamp();
	snd_pcm_format_t convertPcmFormat(uint8_t format);
};

}

#endif /* SRC_ALSAPCM_HPP_ */
