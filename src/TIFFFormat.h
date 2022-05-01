#pragma once

#include <ImageMeta.h>
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
	TIFFFormat();
	virtual ~TIFFFormat();
	bool encodeInit(ImageMeta image, std::string filename, bool asynch, uint32_t concurrency);
	bool encodePixels(uint32_t threadId, uint8_t *pix, uint64_t  offset, uint64_t len, uint32_t index);
	bool encodeFinish(void);
	HeaderInfo getHeaderInfo(void);
	bool close(uint32_t threadId);
	bool close(void);
private:
	bool encodePixels(serialize_buf pixels);
	bool encodeHeader(void);
	bool encodePixelsCoreWrite(serialize_buf pixels);
	bool encodePixelsCore(serialize_buf pixels);
	bool isHeaderEncoded(void);
	TIFF* tif_;
	mutable std::mutex encodePixelmutex_;
	uint32_t encodeState_;
	Serializer serializer_;
	ImageMeta image_;
	std::string filename_;
	std::string mode_;
	TIFFFormatHeaderClassic header_;
	uint32_t concurrency_;
	Serializer **asynchSerializers_;
	std::atomic<uint32_t> numPixelWrites_;
};
