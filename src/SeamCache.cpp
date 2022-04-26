#include <SeamCache.h>

SeamCache::SeamCache(uint32_t numStrips) {
	numSeams_ = numStrips-1;
	seamBuffers_ = new SerializeBuf*[numSeams_];
	for (uint32_t i = 0; i < numSeams_; ++i){
		seamBuffers_[i] = new SerializeBuf();
		seamBuffers_[i]->alloc(WRTSIZE);
	}
}

SeamCache::~SeamCache() {
	for (uint32_t i = 0; i < numSeams_; ++i)
		delete seamBuffers_[i];
	delete[] seamBuffers_;
}

