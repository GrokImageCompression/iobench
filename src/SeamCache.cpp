#include <SeamCache.h>
#include <cassert>

SeamCache::SeamCache(SeamCacheInitInfo initInfo) : init_(initInfo)
{
	assert(init_.numStrips_);
	uint32_t numSeams = init_.numStrips_-1;
	seamBuffers_ = new SerializeBuf*[numSeams];
	for (uint32_t i = 0; i < numSeams; ++i){
		auto inf = getSeamInfo(i);
		if (inf.upperBegin_ != inf.upperEnd_){
			seamBuffers_[i] = new SerializeBuf();
			seamBuffers_[i]->alloc(init_.writeSize_);
		} else {
			seamBuffers_[i] = nullptr;
		}
	}
}
SeamCache::~SeamCache() {
	for (uint32_t i = 0; i < init_.numStrips_-1; ++i)
		delete seamBuffers_[i];
	delete[] seamBuffers_;
}
uint8_t* SeamCache::getSeamBuffer(uint32_t strip){
	if (strip < init_.numStrips_-1){
		return seamBuffers_[strip]->data;
	}

	return nullptr;
}
SeamInfo SeamCache::getSeamInfo(uint32_t strip){
	SeamInfo ret;
	ret.writeSize_ = init_.writeSize_;
	ret.upperBegin_ = upperBegin(strip);
	ret.upperEnd_   = stripEnd(strip);
	ret.lowerBegin_ = stripOffset(strip);
	// no lower seam
	if (strip == 0 ||
			ret.lowerBegin_ % init_.writeSize_ == 0)
		ret.lowerEnd_ = ret.lowerBegin_;
	else
		ret.lowerEnd_ = upperBegin(strip-1) + init_.writeSize_;

	return ret;
}
uint64_t SeamCache::stripOffset(uint32_t strip){
	return strip == 0 ? 0 :
			init_.headerSize_ + strip * nominalStripHeight() * init_.stripPackedByteWidth_;
}
uint32_t SeamCache::nominalStripHeight(void){
	return (init_.height_ + init_.numStrips_-1)/init_.numStrips_;
}
uint32_t SeamCache::stripHeight(uint32_t strip){
	return (strip < init_.numStrips_-1) ? nominalStripHeight() : init_.height_ - strip * nominalStripHeight();
}
uint64_t SeamCache::upperBegin(uint32_t strip){
	return (strip < init_.numStrips_ - 1) ?
			(stripEnd(strip)/init_.writeSize_) * init_.writeSize_ : stripEnd(strip);
}
uint64_t SeamCache::stripLen(uint32_t strip){
	return stripHeight(strip) * init_.stripPackedByteWidth_;
}
uint64_t SeamCache::stripEnd(uint32_t strip){
	return stripOffset(strip) + stripLen(strip);
}
