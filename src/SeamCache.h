#pragma once

#include "IFileIO.h"

class SeamCache {
public:
	SeamCache(uint32_t numStrips);
	virtual ~SeamCache();
private:
	SerializeBuf **seamBuffers_;
	uint32_t numSeams_;
};
