#include <SeamCache.h>
#include <cassert>

SeamCache::SeamCache(SeamCacheInitInfo initInfo) : init_(initInfo),
													numSeams_(init_.imageMeta_.numStrips_-1)
{
	seamBuffers_ = new SerializeBuf*[numSeams_];
	for (uint32_t i = 0; i < numSeams_; ++i){
		auto seamInfo = getSeamInfo(i);
		if (seamInfo.upperBegin_ != seamInfo.upperEnd_){
			seamBuffers_[i] = new SerializeBuf();
			seamBuffers_[i]->alloc(init_.writeSize_);
		} else {
			seamBuffers_[i] = nullptr;
		}
	}
}
SeamCache::~SeamCache() {
	for (uint32_t i = 0; i < numSeams_; ++i){
		auto s = seamBuffers_[i];
		if (s)
		  s->dealloc();
		delete s;
	}
	delete[] seamBuffers_;
}
ImageStripper& SeamCache::imageStripper(void){
	return init_.imageMeta_;
}
//note: there are (numStrips_-1) seam buffers : the final strip has none
uint8_t* SeamCache::getSeamBuffer(uint32_t strip){
	return (strip < imageStripper().numStrips_-1) ? seamBuffers_[strip]->data : nullptr;
}
SeamInfo SeamCache::getSeamInfo(uint32_t strip){
	SeamInfo ret;
	ret.writeSize_ = init_.writeSize_;
	ret.upperBegin_ = upperBegin(strip);
	assert(strip ==  numSeams_ || ret.upperBegin_% init_.writeSize_ == 0);
	ret.upperEnd_   = stripEnd(strip);
	ret.lowerBegin_ = stripOffset(strip);
	// no lower seam
	if (strip == 0 ||  ret.lowerBegin_ % init_.writeSize_ == 0)
		ret.lowerEnd_ = ret.lowerBegin_;
	else
		ret.lowerEnd_ = upperBegin(strip-1) + init_.writeSize_;
	assert(ret.lowerEnd_% init_.writeSize_ == 0);
	assert((strip ==  numSeams_) ||
			(ret.upperBegin_ - ret.lowerEnd_) % init_.writeSize_ == 0);
	ret.numFullBlocks_ = (ret.upperBegin_ - ret.lowerEnd_) / init_.writeSize_;

	return ret;
}
uint64_t SeamCache::stripOffset(uint32_t strip){
	return strip == 0 ? 0 : init_.headerSize_ + imageStripper().stripOffset(strip);
}
uint64_t SeamCache::stripEnd(uint32_t strip){
	return stripOffset(strip) + imageStripper().stripLen(strip);
}
uint64_t SeamCache::upperBegin(uint32_t strip){
	return (strip < numSeams_) ?
			(stripEnd(strip)/init_.writeSize_) * init_.writeSize_ : stripEnd(strip);
}

