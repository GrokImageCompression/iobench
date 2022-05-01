#pragma once

#include "IFileIO.h"
#include "ImageMeta.h"

struct SeamInfo{
	SeamInfo() :    lowerBegin_(0), lowerEnd_(0),
					upperBegin_(0), upperEnd_(0), numWriteBlocks_(0),
					writeSize_(0)
	{}
	uint64_t lowerBegin_;
	uint64_t lowerEnd_;
	uint64_t upperBegin_;
	uint64_t upperEnd_;
	uint32_t numWriteBlocks_;
	uint64_t writeSize_;
};

struct SeamCacheInitInfo{
	SeamCacheInitInfo() : headerSize_(0), writeSize_(0)
	{}
	uint64_t headerSize_;
	uint64_t writeSize_;
	ImageMeta imageMeta_;
};

class SeamCache {
public:
	SeamCache(SeamCacheInitInfo initInfo);
	virtual ~SeamCache();
	SeamInfo getSeamInfo(uint32_t strip);
	uint8_t* getSeamBuffer(uint32_t strip);
private:
	uint64_t stripOffset(uint32_t strip);
	uint64_t stripEnd(uint32_t strip);
	uint64_t upperBegin(uint32_t strip);
	ImageMeta& imageMeta(void);
	SerializeBuf **seamBuffers_;
	SeamCacheInitInfo init_;
};
