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
	const uint64_t bytes_total = (uint64_t)size;
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

TIFFFormat::TIFFFormat(bool asynch) : tif_(nullptr), encodeState_(IMAGE_FORMAT_UNENCODED),
							serializer_(!asynch), imageStripper_(nullptr),
							concurrency_(0), workerSerializers_(nullptr),
							numPixelWrites_(0)
{}

TIFFFormat::~TIFFFormat() {
	close();
	if (workerSerializers_){
		for (uint32_t i = 0; i < concurrency_; ++i)
			delete workerSerializers_[i];
		delete[] workerSerializers_;
	}
	delete imageStripper_;
}
void TIFFFormat::init(uint32_t width, uint32_t height,
						uint16_t numcomps, uint32_t nominalStripHeight,
						bool chunked){
	imageStripper_ = new ImageStripper(width, height,1,32,
						sizeof(header_), WRTSIZE, chunked ? serializer_.getPool(): nullptr);
}
ImageStripper* TIFFFormat::getImageStripper(void){
	return imageStripper_;
}
// corrected for header
IOBuf* TIFFFormat::getPoolBuffer(uint32_t threadId,uint32_t strip){
	auto chunkInfo = imageStripper_->getChunkInfo(strip);
	uint64_t len = chunkInfo.len();
	auto ioBuf = workerSerializers_[threadId]->getPoolBuffer(len);
	ioBuf->index_ = strip;
	ioBuf->offset_ = chunkInfo.first_.x0_;
	uint64_t headerSize = ((strip == 0) ? sizeof(header_) : 0);
	ioBuf->skip_ = 0;
	if (headerSize) {
		memcpy(ioBuf->data_ , &header_, headerSize);
		ioBuf->skip_ = headerSize;
	}
	assert(ioBuf->data_);

	return ioBuf;
}
StripChunkArray* TIFFFormat::getStripChunkArray(uint32_t threadId,uint32_t strip){
	auto pool = workerSerializers_[threadId]->getPool();
	return
		imageStripper_->getStrip(strip)->getStripChunkArray(pool,
								strip == 0 ? (uint8_t*)&header_ : nullptr,
								strip == 0 ? sizeof(header_) : 0);
}
bool TIFFFormat::encodeInit(std::string filename,
							bool direct,
							uint32_t concurrency,
							bool asynch){
	filename_ = filename;
	concurrency_ = concurrency;
	auto maxRequests = imageStripper_->numStrips();
	serializer_.setMaxPooledRequests(maxRequests);
	bool rc;
	mode_ = direct ? "wd" : "w";
	if(!serializer_.open(filename_, mode_,asynch))
		return false;
	serializer_.registerApplicationClient();
	// create one serializer per thread and attach to parent serializer
	workerSerializers_ = new Serializer*[concurrency];
	for (uint32_t i = 0; i < concurrency_; ++i){
		workerSerializers_[i] = new Serializer(false);
		workerSerializers_[i]->registerApplicationClient();
		workerSerializers_[i]->attach(&serializer_);
	}

	return true;
}
bool TIFFFormat::encodePixels(uint32_t threadId, IOBuf **buffers,
													uint32_t numBuffers){
	Serializer *ser = workerSerializers_[threadId];
	uint64_t toWrite = 0;
	for (uint32_t i = 0; i < numBuffers; ++i)
		toWrite += buffers[i]->len_;
	uint64_t written = ser->write(buffers[0]->offset_, buffers,numBuffers);
	if (written != toWrite){
		printf("encodePixels: "
				"attempted to write %d, "
				"actually wrote %d, difference %d\n",
				toWrite,written,
				toWrite - written);
		return false;
	}

	return true;

}
bool TIFFFormat::close(void){
	for (uint32_t i = 0; i < concurrency_; ++i)
		workerSerializers_[i]->close();
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
