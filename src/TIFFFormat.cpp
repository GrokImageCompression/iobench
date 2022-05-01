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
							concurrency_(0), asynchSerializers_(nullptr),
							numPixelWrites_(0)
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
bool TIFFFormat::encodeInit(ImageMeta image,
							std::string filename,
							SerializeState serializeState,
							uint32_t concurrency){
	image_ = image;
	filename_ = filename;
	concurrency_ = concurrency;
	auto maxRequests = image.numStrips_;
	serializer_.setMaxPooledRequests(maxRequests);
	bool rc;
	mode_ = "w";
	if(!serializer_.open(filename_, mode_,serializeState))
		return false;
	if (serializeState == SERIALIZE_STATE_ASYNCH_WRITE){
		serializer_.registerApplicationClient();
		// create one serializer per thread and attach to parent serializer
		asynchSerializers_ = new Serializer*[concurrency];
		for (uint32_t i = 0; i < concurrency_; ++i){
			asynchSerializers_[i] = new Serializer();
			asynchSerializers_[i]->attach(&serializer_);
			asynchSerializers_[i]->setHeader((uint8_t*)&header_, sizeof(header_));
		}
		rc = true;
	} else {
		tif_ =   TIFFClientOpen(filename_.c_str(),
				mode_.c_str(), &serializer_, TiffRead, TiffWrite,
					TiffSeek, TiffClose,
						TiffSize, nullptr, nullptr);
		rc = tif_ != nullptr;
	}

	return rc;
}
bool TIFFFormat::close(uint32_t threadId){
	return asynchSerializers_[threadId]->close();
}
bool TIFFFormat::encodePixels(uint32_t threadId, uint8_t *pix, uint64_t offset,
								uint64_t len, uint32_t index){
	switch(serializer_.getState() ){
		case SERIALIZE_STATE_ASYNCH_WRITE:
		{
			//1. schedule write
			auto ser = asynchSerializers_[threadId];
			auto written = ser->writeAsynch(pix,index == 0 ? 0 : sizeof(header_) + offset,len,index);
			if (written != len){
				printf("Error writing strip\n");
				return false;
			}

			return true;
		}
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
bool TIFFFormat::close(void){
	if (asynchSerializers_){
		for (uint32_t i = 0; i < concurrency_; ++i)
			asynchSerializers_[i]->close();
	}
	if(tif_) {
		TIFFClose(tif_);
		tif_ = nullptr;
	}
	serializer_.close();

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

	if (serializer_.getState() == SERIALIZE_STATE_ASYNCH_WRITE){
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
		for(uint32_t j = 0; j < image_.numStrips_; ++j){
			uint64_t len =
					(j == image_.numStrips_ - 1 && image_.finalStripLen_) ? image_.finalStripLen_ : image_.stripLen_;
			//fprintf(stderr,"TIFF initiate sim write %d\n",j);
			tmsize_t written =
				TIFFWriteEncodedStrip(tif_, j, nullptr, (tmsize_t)len);
			if (written == -1){
				printf("Error writing strip\n");
				return false;
			}
		}
	}

	close();
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
