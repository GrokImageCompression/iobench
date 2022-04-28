#include "TIFFFormat.h"
#include "tiffiop.h"
#define IO_MAX 2147483647U

static bool applicationReclaimCallback(serialize_buf buffer, void* serialize_user_data)
{
	auto pool = (BufferPool*)serialize_user_data;
	if(pool)
		pool->put(SerializeBuf(buffer));

	return true;
}

static tmsize_t TiffRead(thandle_t handle, void* buf, tmsize_t size)
{
	(void)(handle);
	(void)(buf);

	return size;
}
static tmsize_t TiffWrite(thandle_t handle, void* buf, tmsize_t size)
{
	auto* serializer_ = (Serializer*)handle;
	const size_t bytes_total = (size_t)size;
	if((tmsize_t)bytes_total != size)
	{
		errno = EINVAL;
		return (tmsize_t)-1;
	}

	if(serializer_->write((uint8_t*)buf, bytes_total) == bytes_total)
		return size;
	else
		return (tmsize_t)-1;
}

static uint64_t TiffSeek(thandle_t handle, uint64_t off, int32_t whence)
{
	auto* serializer_ = (Serializer*)handle;
	_TIFF_off_t off_io = (_TIFF_off_t)off;

	if((uint64_t)off_io != off)
	{
		errno = EINVAL;
		return (uint64_t)-1;
	}

	return serializer_->seek((int64_t)off, whence);
}

static int TiffClose(thandle_t handle)
{
	auto* serializer_ = (Serializer*)handle;

	return serializer_->close() ? 0 : EINVAL;
}

static uint64_t TiffSize(thandle_t handle)
{
	(void)(handle);

	return 0U;
}

TIFFFormat::TIFFFormat() : tif_(nullptr), encodeState_(IMAGE_FORMAT_UNENCODED){

}

TIFFFormat::~TIFFFormat() {
	if(tif_)
		TIFFClose(tif_);
}
HeaderInfo TIFFFormat::getHeader(){
	HeaderInfo ret((uint8_t*)&header_,sizeof(TIFFFormatHeaderClassic) );

	return ret;
}

void TIFFFormat::serializeRegisterClientCallback(serialize_callback reclaim_callback,
												  void* user_data)
{
	serializer_.serializeRegisterClientCallback(reclaim_callback, user_data);
}
void TIFFFormat::serializeRegisterApplicationClient(void)
{
	serializeRegisterClientCallback(applicationReclaimCallback, &pool_);
}
void TIFFFormat::serializeReclaimBuffer(serialize_buf buffer)
{
	auto cb = serializer_.getSerializerReclaimCallback();
	if(cb)
		cb(buffer, serializer_.getSerializerReclaimUserData());
}

bool TIFFFormat::encodeInit(Image image, std::string filename, bool asynch){
	image_ = image;
	filename_ = filename;
	tif_ =  MyTIFFOpen(filename.c_str(), "w", asynch);
	auto maxRequests = (image_.height_ + image_.rowsPerStrip_ - 1) / image_.rowsPerStrip_;
	serializer_.setMaxPooledRequests(maxRequests);
	serializeRegisterApplicationClient();

	return tif_ != nullptr;
}
bool TIFFFormat::encodePixels(uint8_t *pix, uint64_t len, uint32_t index){
	auto b = pool_.get(len);
	b.pooled = true;
	b.index = index;
	memcpy(b.data, pix,len);

	return encodePixels(b);
}
TIFF* TIFFFormat::MyTIFFOpen(const char* name, const char* mode, bool asynch)
{
	if(!serializer_.open(name, mode,asynch))
		return ((TIFF*)0);
	auto tif = TIFFClientOpen(name, mode, &serializer_, TiffRead, TiffWrite, TiffSeek, TiffClose,
							  TiffSize, nullptr, nullptr);
	if(!tif)
		serializer_.close();

	return tif;
}
bool TIFFFormat::encodePixelsCoreWrite(serialize_buf pixels)
{
	tmsize_t written =
		TIFFWriteEncodedStrip(tif_, pixels.index, pixels.data, (tmsize_t)pixels.dataLen);

	return written != -1;
}
bool TIFFFormat::encodeHeader(void){
	if(isHeaderEncoded())
		return true;

	TIFFSetField(tif_, TIFFTAG_IMAGEWIDTH, image_.width_);
	TIFFSetField(tif_, TIFFTAG_IMAGELENGTH, image_.height_);
	TIFFSetField(tif_, TIFFTAG_SAMPLESPERPIXEL, image_.numcomps_);
	TIFFSetField(tif_, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif_, TIFFTAG_PHOTOMETRIC, image_.numcomps_ == 3 ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK);
	TIFFSetField(tif_, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif_, TIFFTAG_ROWSPERSTRIP, image_.rowsPerStrip_);

	encodeState_ = IMAGE_FORMAT_ENCODED_HEADER;

	return true;
}
/***
 * library-orchestrated pixel encoding
 */
bool TIFFFormat::encodePixels(serialize_buf pixels)
{
	std::unique_lock<std::mutex> lk(encodePixelmutex_);
	if(encodeState_ & IMAGE_FORMAT_ENCODED_PIXELS)
		return true;
	if(!isHeaderEncoded() && !encodeHeader())
		return false;

	return encodePixelsCore(pixels);
}
bool TIFFFormat::isHeaderEncoded(void)
{
	return ((encodeState_ & IMAGE_FORMAT_ENCODED_HEADER) == IMAGE_FORMAT_ENCODED_HEADER);
}
bool TIFFFormat::encodeFinish(void)
{
	if(encodeState_ & IMAGE_FORMAT_ENCODED_PIXELS)
	{
		assert(!tif_);
		return true;
	}
	if(tif_)
		TIFFClose(tif_);
	tif_ = nullptr;
	encodeState_ |= IMAGE_FORMAT_ENCODED_PIXELS;

	return true;
}
/***
 * Common core pixel encoding
 */
bool TIFFFormat::encodePixelsCore(serialize_buf pixels)
{
	(void)(pixels);
	serializer_.initPooledRequest();
	bool success = encodePixelsCoreWrite(pixels);
	if(success)
	{
		if(serializer_.allPooledRequestsComplete())
			encodeFinish();
	}
	else
	{
		printf("TIFFFormat::encodePixelsCore: error in pixels encode");
		encodeState_ |= IMAGE_FORMAT_ERROR;
	}

	return success;
}
