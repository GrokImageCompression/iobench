#pragma once

#include "Serializer.h"
#include "BufferPool.h"
#include "Image.h"
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
	bool encodeInit(Image image, std::string filename, bool asynch);
	void serializeRegisterClientCallback(serialize_callback reclaim_callback,
										 void* user_data);
	void serializeReclaimBuffer(serialize_buf buffer);
	void serializeRegisterApplicationClient(void);
	bool encodePixels(uint8_t *pix, uint64_t len, uint32_t index);
	HeaderInfo getHeaderInfo(void);
private:
	bool encodePixels(serialize_buf pixels);
	TIFF* MyTIFFOpen(const char* name, const char* mode, bool asynch);
	bool encodeHeader(void);
	bool encodePixelsCoreWrite(serialize_buf pixels);
	bool encodePixelsCore(serialize_buf pixels);
	bool encodeFinish(void);
	bool isHeaderEncoded(void);
	TIFF* tif_;
	mutable std::mutex encodePixelmutex_;
	uint32_t encodeState_;
	BufferPool pool_;
	Serializer serializer_;
	Image image_;
	std::string filename_;
	TIFFFormatHeaderClassic header_;
};
