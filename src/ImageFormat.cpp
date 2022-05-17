#include <ImageFormat.h>

ImageFormat::ImageFormat(bool flushOnClose, uint8_t *header, size_t headerLength) :
							header_(header),
							headerLength_(headerLength),
							encodeState_(IMAGE_FORMAT_UNENCODED),
							serializer_(flushOnClose),
							imageStripper_(nullptr),
							concurrency_(0),
							workerSerializers_(nullptr),
							numPixelWrites_(0)
{}
ImageFormat::~ImageFormat() {
	close();
	if (workerSerializers_){
		for (uint32_t i = 0; i < concurrency_; ++i)
			delete workerSerializers_[i];
		delete[] workerSerializers_;
	}
	delete imageStripper_;
}
void ImageFormat::init(uint32_t width, uint32_t height,
						uint16_t numcomps, uint32_t nominalStripHeight,
						bool chunked){
	imageStripper_ = new ImageStripper(width, height,1,32,
						headerLength_,
						WRTSIZE, chunked ? serializer_.getPool(): nullptr);
}
bool ImageFormat::encodeInit(std::string filename,
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
ImageStripper* ImageFormat::getImageStripper(void){
	return imageStripper_;
}
// corrected for header
IOBuf* ImageFormat::getPoolBuffer(uint32_t threadId,uint32_t strip){
	auto chunkInfo = imageStripper_->getChunkInfo(strip);
	uint64_t len = chunkInfo.len();
	auto ioBuf = workerSerializers_[threadId]->getPoolBuffer(len);
	ioBuf->index_ = strip;
	ioBuf->offset_ = chunkInfo.first_.x0_;
	uint64_t headerSize = ((strip == 0) ? headerLength_ : 0);
	ioBuf->skip_ = 0;
	if (headerSize) {
		memcpy(ioBuf->data_ , header_, headerSize);
		ioBuf->skip_ = headerSize;
	}
	assert(ioBuf->data_);

	return ioBuf;
}
StripChunkArray* ImageFormat::getStripChunkArray(uint32_t threadId,uint32_t strip){
	auto pool = workerSerializers_[threadId]->getPool();
	return
		imageStripper_->getStrip(strip)->getStripChunkArray(pool,
								strip == 0 ? header_ : nullptr,
								strip == 0 ? headerLength_ : 0);
}
bool ImageFormat::encodePixels(uint32_t threadId,
								IOBuf **buffers,
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
bool ImageFormat::isHeaderEncoded(void)
{
	return ((encodeState_ & IMAGE_FORMAT_ENCODED_HEADER) == IMAGE_FORMAT_ENCODED_HEADER);
}
bool ImageFormat::close(void){
	// close all thread serializers
	for (uint32_t i = 0; i < concurrency_; ++i)
		workerSerializers_[i]->close();

	return true;
}
