#pragma once

#include <ImageStripper.h>
#include "Serializer.h"
#include "BufferPool.h"
#include <string>
#include <tiffio.h>

const uint32_t IMAGE_FORMAT_UNENCODED = 1;
const uint32_t IMAGE_FORMAT_ENCODED_HEADER = 2;
const uint32_t IMAGE_FORMAT_ENCODED_PIXELS = 4;
const uint32_t IMAGE_FORMAT_ERROR = 8;

struct TIFFFormatHeaderClassic {
	TIFFFormatHeaderClassic() : tiff_magic(0x4949), tiff_version(42),tiff_diroff(0)
	{}
	uint16_t tiff_magic;      /* magic number (defines byte order) */
	uint16_t tiff_version;    /* TIFF version number */
	uint32_t tiff_diroff;     /* byte offset to first directory */
};

struct HeaderInfo{
	HeaderInfo(uint8_t *header,uint32_t length ) : header_(header), length_(length)
	{}
	uint8_t *header_;
	uint32_t length_;
};

class TIFFFormat {
public:
	TIFFFormat(bool asynch, bool flushOnClose);
	virtual ~TIFFFormat();
	void init(uint32_t width, uint32_t height,
				uint16_t numcomps, uint32_t nominalStripHeight,
				bool chunked);
	bool encodeInit(std::string filename,bool direct, uint32_t concurrency,bool asynch);
	bool encodePixels(uint32_t threadId, IOBuf **buffers, uint32_t numBuffers);
	bool encodeFinish(void);
	bool close(void);
	IOBuf* getPoolBuffer(uint32_t threadId,uint32_t strip);
	StripChunkArray* getStripChunkArray(uint32_t threadId,uint32_t strip);
	ImageStripper* getImageStripper(void);

private:
	bool encodePixels(io_buf pixels);
	bool encodeHeader(void);
	bool isHeaderEncoded(void);
	TIFF* tif_;
	uint32_t encodeState_;
	Serializer serializer_;
	ImageStripper* imageStripper_;
	std::string filename_;
	std::string mode_;
	TIFFFormatHeaderClassic header_;
	uint32_t concurrency_;
	Serializer **workerSerializers_;
	std::atomic<uint32_t> numPixelWrites_;
};
