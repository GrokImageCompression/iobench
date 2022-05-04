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
							numPixelWrites_(0), seamCache_(nullptr)
{}

TIFFFormat::~TIFFFormat() {
	close();
	if (asynchSerializers_){
		for (uint32_t i = 0; i < concurrency_; ++i)
			delete asynchSerializers_[i];
		delete[] asynchSerializers_;
	}
	delete seamCache_;
}
SerializeBuf TIFFFormat::getPoolBuffer(uint32_t threadId,uint32_t index){
	auto seamInfo = seamCache_->getSeamInfo(index);
	uint64_t len = seamInfo.upperEnd_ - seamInfo.lowerBegin_;
	uint64_t headerSize = ((index == 0) ? sizeof(header_) : 0);
	uint64_t totalLength = len + headerSize;
	SerializeBuf  serializeBuf =
			asynchSerializers_ ? asynchSerializers_[threadId]->getPoolBuffer(totalLength) :
					serializer_.getPoolBuffer(totalLength);
	serializeBuf.index = index;
	serializeBuf.offset = seamInfo.lowerBegin_;
	if (headerSize) {
		memcpy(serializeBuf.data , &header_, headerSize);
		serializeBuf.skip = headerSize;
	}
	serializeBuf.pooled = true;

	return serializeBuf;
}
bool TIFFFormat::encodeInit(ImageStripper image,
							std::string filename,
							bool asynch,
							uint32_t concurrency){
	SeamCacheInitInfo
		seamCacheInitInfo(sizeof(header_), WRTSIZE, image);
	seamCache_ = new SeamCache(seamCacheInitInfo);
	image_ = image;
	filename_ = filename;
	concurrency_ = concurrency;
	auto maxRequests = image.numStrips_;
	serializer_.setMaxPooledRequests(maxRequests);
	bool rc;
	mode_ = "w";
	if(!serializer_.open(filename_, mode_,asynch))
		return false;
	serializer_.registerApplicationClient();
	if (asynch){
		// create one serializer per thread and attach to parent serializer
		asynchSerializers_ = new Serializer*[concurrency];
		for (uint32_t i = 0; i < concurrency_; ++i){
			asynchSerializers_[i] = new Serializer();
			asynchSerializers_[i]->attach(&serializer_);
		}
	}

	return true;
}
bool TIFFFormat::encodePixels(uint32_t threadId, SerializeBuf serializeBuf){
	Serializer *ser = asynchSerializers_ ? asynchSerializers_[threadId] : &serializer_;
	// use seam cache to break strip down into write blocks + seams
	//1. write bottom seam

	//2. write full blocks

	//3. write top seam
	size_t written = ser->write(serializeBuf);
	if (written != serializeBuf.dataLen){
		printf("encodePixels: write error\n");
		return false;
	}

	return true;

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
bool TIFFFormat::encodeHeader(void){
	if(isHeaderEncoded())
		return true;

	TIFFSetField(tif_, TIFFTAG_IMAGEWIDTH, image_.width_);
	TIFFSetField(tif_, TIFFTAG_IMAGELENGTH, image_.height_);
	TIFFSetField(tif_, TIFFTAG_SAMPLESPERPIXEL, image_.numcomps_);
	TIFFSetField(tif_, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif_, TIFFTAG_PHOTOMETRIC, image_.numcomps_ == 3 ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK);
	TIFFSetField(tif_, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif_, TIFFTAG_ROWSPERSTRIP, image_.nominalStripHeight_);

	encodeState_ = IMAGE_FORMAT_ENCODED_HEADER;

	return true;
}
bool TIFFFormat::isHeaderEncoded(void)
{
	return ((encodeState_ & IMAGE_FORMAT_ENCODED_HEADER) == IMAGE_FORMAT_ENCODED_HEADER);
}
bool TIFFFormat::encodeFinish(void)
{
	if(filename_.empty() || (encodeState_ & IMAGE_FORMAT_ENCODED_PIXELS))
	{
		assert(!tif_);
		return true;
	}

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
	for(uint32_t j = 0; j < image_.numStrips_; ++j){
		tmsize_t written =
			TIFFWriteEncodedStrip(tif_, j, nullptr, (tmsize_t)image_.stripLen(j));
		if (written == -1){
			printf("Error writing strip\n");
			return false;
		}
	}


	close();
	encodeState_ |= IMAGE_FORMAT_ENCODED_PIXELS;

	return true;
}
