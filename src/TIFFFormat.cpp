#include "TIFFFormat.h"
#include "tiffiop.h"

static tmsize_t TiffRead(thandle_t handle, void* buf, tmsize_t size)
{
	(void)handle;
	(void)buf;

	return size;
}
static tmsize_t TiffWrite(thandle_t handle, void* buf, tmsize_t size)
{
	auto* serializer_ = (Serializer*)handle;
	const uint64_t bytes_total = (uint64_t)size;
	if(bytes_total != size)
	{
		errno = EINVAL;
		return (tmsize_t)-1;
	}

	if(serializer_->write((uint8_t*)buf, bytes_total) == bytes_total)
		return size;
	else
		return (tmsize_t)-1;
}

static toff_t TiffSeek(thandle_t handle, toff_t off, int whence)
{
	auto* serializer_ = (Serializer*)handle;

	if((int64_t)off != off)
	{
		errno = EINVAL;
		return (uint64_t)-1;
	}

	return serializer_->seek((int64_t)off, whence);
}

static int TiffClose(thandle_t handle)
{
	return ((Serializer*)handle)->close() ? 0 : EINVAL;
}

static toff_t TiffSize(thandle_t handle)
{
	(void)handle;

	return 0U;
}

TIFFFormat::TIFFFormat(bool asynch, bool flushOnClose) :
							ImageFormat(flushOnClose,
										(uint8_t*)&header_,
										sizeof(header_)),
							tif_(nullptr)
{}

bool TIFFFormat::close(void){
	if (!ImageFormat::close())
		return false;

	if(tif_) {
		TIFFClose(tif_);
		tif_ = nullptr;
	}

	return true;
}
bool TIFFFormat::encodeHeader(void){
	if(isHeaderEncoded())
		return true;

	TIFFSetField(tif_, TIFFTAG_IMAGEWIDTH, imageStripper_->width_);
	TIFFSetField(tif_, TIFFTAG_IMAGELENGTH, imageStripper_->height_);
	TIFFSetField(tif_, TIFFTAG_SAMPLESPERPIXEL, imageStripper_->numcomps_);
	TIFFSetField(tif_, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif_, TIFFTAG_PHOTOMETRIC, imageStripper_->numcomps_ == 3 ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK);
	TIFFSetField(tif_, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif_, TIFFTAG_ROWSPERSTRIP, imageStripper_->nominalStripHeight_);

	encodeState_ = IMAGE_FORMAT_ENCODED_HEADER;

	return true;
}
bool TIFFFormat::encodeFinish(void)
{
	if(filename_.empty() || (encodeState_ & IMAGE_FORMAT_ENCODED_PIXELS))
		return true;

	serializer_.enableSimulateWrite();
	// 1. open tiff and encode header
	tif_ =   TIFFClientOpen(filename_.c_str(),
			"w", &serializer_, TiffRead, TiffWrite,
				TiffSeek, TiffClose,
					TiffSize, nullptr, nullptr);
	if (!tif_)
		return false;
	if (!encodeHeader())
		return false;

	//2. simulate strip writes
	for(uint32_t j = 0; j < imageStripper_->numStrips(); ++j){
		tmsize_t written =
			TIFFWriteEncodedStrip(tif_, j, nullptr, (tmsize_t)imageStripper_->getStrip(j)->logicalLen_);
		if (written == -1){
			printf("Error writing strip\n");
			return false;
		}
	}
	close();
	encodeState_ |= IMAGE_FORMAT_ENCODED_PIXELS;

	return true;
}
