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
uint8_t* SeamCache::getSeamBuffer(uint32_t stripIndex){
	if (stripIndex < init_.numStrips_-1){
		return seamBuffers_[stripIndex]->data;
	}

	return nullptr;
}
SeamInfo SeamCache::getSeamInfo(uint32_t stripIndex){
	SeamInfo ret;
	ret.writeSize_ = init_.writeSize_;
	ret.upperBegin_ = upperBegin(stripIndex);
	ret.upperEnd_   = stripEnd(stripIndex);
	ret.lowerBegin_ = stripOffset(stripIndex);
	// no lower seam
	if (stripIndex == 0 ||
			ret.lowerBegin_ % init_.writeSize_ == 0)
		ret.lowerEnd_ = ret.lowerBegin_;
	else
		ret.lowerEnd_ = upperBegin(stripIndex-1) + init_.writeSize_;

	return ret;
}
uint64_t SeamCache::stripOffset(uint32_t stripIndex){
	return stripIndex == 0 ? 0 :
			init_.headerSize_ + stripIndex * nominalStripHeight() * init_.stripPackedByteWidth_;
}
uint32_t SeamCache::nominalStripHeight(void){
	return (init_.height_ + init_.numStrips_-1)/init_.numStrips_;
}
uint32_t SeamCache::stripHeight(uint32_t stripIndex){
	return (stripIndex < init_.numStrips_-1) ? nominalStripHeight() : init_.height_ - stripIndex * nominalStripHeight();
}
uint64_t SeamCache::upperBegin(uint32_t stripIndex){
	return (stripIndex < init_.numStrips_ - 1) ?
			(stripEnd(stripIndex)/init_.writeSize_) * init_.writeSize_ : stripEnd(stripIndex);
}
uint64_t SeamCache::stripLen(uint32_t stripIndex){
	return stripHeight(stripIndex) * init_.stripPackedByteWidth_;
}
uint64_t SeamCache::stripEnd(uint32_t stripIndex){
	return stripOffset(stripIndex) + stripLen(stripIndex);
}
