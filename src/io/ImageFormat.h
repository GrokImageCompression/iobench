/*
 *    Copyright (C) 2022 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#pragma once

#include <string>
#include <functional>

#include "ImageStripper.h"
#include "Serializer.h"
#include "BufferPool.h"

namespace io {

const uint32_t IMAGE_FORMAT_UNENCODED = 1;
const uint32_t IMAGE_FORMAT_ENCODED_HEADER = 2;
const uint32_t IMAGE_FORMAT_ENCODED_PIXELS = 4;
const uint32_t IMAGE_FORMAT_ERROR = 8;

class ImageFormat {
public:
	ImageFormat(bool flushOnClose,
				uint8_t *header,
				size_t headerLength);
	virtual ~ImageFormat();
	void registerReclaimCallback(io_callback reclaim_callback, void* user_data);
	virtual bool close(void);
	void setEncodeFinisher(std::function<bool(void)> finisher);
	virtual void init(uint32_t width,
						uint32_t height,
						uint16_t numcomps,
						uint64_t packedRowBytes,
						uint32_t nominalStripHeight,
						bool chunked);
	bool reopenAsBuffered(void);
	virtual bool encodeInit(std::string filename,
							bool direct,
							uint32_t concurrency,
							bool asynch);
	virtual bool encodePixels(uint32_t threadId,
								IOBuf **buffers,
								uint32_t numBuffers);
	virtual bool encodePixels(uint32_t threadId,StripChunkArray * chunkArray);
	virtual bool encodeFinish(void) = 0;
	IOBuf* getPoolBuffer(uint32_t threadId,uint32_t strip);
	StripChunkArray* getStripChunkArray(uint32_t threadId,uint32_t strip);
	ImageStripper* getImageStripper(void);
protected:
	bool closeThreadSerializers(void);
	bool isHeaderEncoded(void);
	uint8_t *header_;
	size_t headerLength_;
	uint32_t encodeState_;
	Serializer serializer_;
	ImageStripper* imageStripper_;
	std::string filename_;
	std::string mode_;
	uint32_t concurrency_;
	Serializer **workerSerializers_;
	std::atomic<uint64_t> numPixelWrites_;
	uint64_t maxPixelWrites_;
	std::function<bool(void)> encodeFinisher_;
};

}
