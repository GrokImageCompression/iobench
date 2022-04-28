#include <SeamCache.h>
#include <cassert>

SeamCache::SeamCache(SeamCacheInitInfo initInfo) : init_(initInfo),
		numStrips_((init_.height_ + init_.nominalStripHeight_ - 1)/	init_.nominalStripHeight_)
{
	uint32_t numSeams = numStrips_-1;
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
	for (uint32_t i = 0; i < numStrips_-1; ++i)
		delete seamBuffers_[i];
	delete[] seamBuffers_;
}
//note: there are (numStrips_-1) seam buffers : the final strip has none
uint8_t* SeamCache::getSeamBuffer(uint32_t strip){
	return (strip < numStrips_-1) ? seamBuffers_[strip]->data : nullptr;
}
SeamInfo SeamCache::getSeamInfo(uint32_t strip){
	SeamInfo ret;
	ret.writeSize_ = init_.writeSize_;
	ret.upperBegin_ = upperBegin(strip);
	assert(ret.upperBegin_% init_.writeSize_ == 0);
	ret.upperEnd_   = stripEnd(strip);
	ret.lowerBegin_ = stripOffset(strip);
	// no lower seam
	if (strip == 0 ||
			ret.lowerBegin_ % init_.writeSize_ == 0)
		ret.lowerEnd_ = ret.lowerBegin_;
	else
		ret.lowerEnd_ = upperBegin(strip-1) + init_.writeSize_;
	assert(ret.lowerEnd_% init_.writeSize_ == 0);
	assert((ret.upperBegin_ - ret.lowerEnd_) % init_.writeSize_ == 0);
	ret.numWriteBlocks_ = (ret.upperBegin_ - ret.lowerEnd_) / init_.writeSize_;

	return ret;
}
uint32_t SeamCache::getNumStrips(void){
	return numStrips_;
}
uint64_t SeamCache::stripOffset(uint32_t strip){
	return strip == 0 ? 0 :
			init_.headerSize_ + strip * init_.nominalStripHeight_ * init_.stripPackedByteWidth_;
}
uint32_t SeamCache::stripHeight(uint32_t strip){
	return (strip < numStrips_-1) ? init_.nominalStripHeight_ :
			init_.height_ - strip * init_.nominalStripHeight_;
}
uint64_t SeamCache::upperBegin(uint32_t strip){
	return (strip < numStrips_ - 1) ?
			(stripEnd(strip)/init_.writeSize_) * init_.writeSize_ : stripEnd(strip);
}
uint64_t SeamCache::stripLen(uint32_t strip){
	return stripHeight(strip) * init_.stripPackedByteWidth_;
}
uint64_t SeamCache::stripEnd(uint32_t strip){
	return stripOffset(strip) + stripLen(strip);
}
