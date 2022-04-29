#include "TIFFFormat.h"
#include "tiffiop.h"
#define IO_MAX 2147483647U

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

TIFFFormat::TIFFFormat() : tif_(nullptr), encodeState_(IMAGE_FORMAT_UNENCODED),
							concurrency_(0), asynchSerializers_(nullptr)
{}

TIFFFormat::~TIFFFormat() {
	close();
	if (asynchSerializers_){
		for (uint32_t i = 0; i < concurrency_; ++i)
			delete asynchSerializers_[i];
		delete[] asynchSerializers_;
	}
}
HeaderInfo TIFFFormat::getHeaderInfo(){
	return  HeaderInfo((uint8_t*)&header_,sizeof(TIFFFormatHeaderClassic) );
}
bool TIFFFormat::encodeInit(Image image,
							std::string filename,
							SerializeState serializeState,
							uint32_t concurrency){
	image_ = image;
	filename_ = filename;
	concurrency_ = concurrency;
	auto maxRequests = (image_.height_ + image_.rowsPerStrip_ - 1) / image_.rowsPerStrip_;
	serializer_.setMaxPooledRequests(maxRequests);
	serializer_.registerApplicationClient();
	bool rc;
	if (serializeState == SERIALIZE_STATE_ASYNCH_WRITE){
		if(!serializer_.open(filename, "wd",SERIALIZE_STATE_ASYNCH_WRITE))
			return false;
		// create one serializer per thread and attach to parent serializer
		asynchSerializers_ = new Serializer*[concurrency];
		for (uint32_t i = 0; i < concurrency_; ++i){
			asynchSerializers_[i] = new Serializer();
			asynchSerializers_[i]->attach(&serializer_);
		}
		rc = true;
	} else {
		tif_ =  MyTIFFOpen(filename.c_str(), "w", SERIALIZE_STATE_SYNCH);
		rc = tif_ != nullptr;
	}

	return rc;
}
bool TIFFFormat::encodePixels(uint32_t threadId, uint8_t *pix, uint64_t offset,
								uint64_t len, uint32_t index){
	switch(serializer_.getState() ){
		case SERIALIZE_STATE_ASYNCH_WRITE:
		{
			auto ser = asynchSerializers_[threadId];
			auto written = ser->write(pix,offset,len);
			return written == len;
		}
			break;
		case SERIALIZE_STATE_SYNCH_SIM_PIXEL_WRITE:
			break;
		case SERIALIZE_STATE_SYNCH:
		{
			auto b = serializer_.getPoolBuffer(len);
			b.pooled = true;
			b.index = index;
			memcpy(b.data, pix,len);
			return encodePixels(b);
		}
			break;
	}

	return false;
}
TIFF* TIFFFormat::MyTIFFOpen(std::string name, std::string mode, SerializeState serializeState)
{
	if(!serializer_.open(name, mode,serializeState))
		return ((TIFF*)0);
	auto tif = TIFFClientOpen(name.c_str(),
							mode.c_str(), &serializer_, TiffRead, TiffWrite, TiffSeek, TiffClose,
							  TiffSize, nullptr, nullptr);
	if(!tif)
		serializer_.close();

	return tif;
}
bool TIFFFormat::close(void){
	serializer_.close();
	if (asynchSerializers_){
		for (uint32_t i = 0; i < concurrency_; ++i)
			asynchSerializers_[i]->close();
	}
	if(tif_)
		TIFFClose(tif_);

	return true;
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
