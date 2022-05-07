#pragma once

#include "IFileIO.h"
#include "ImageStripper.h"

struct ChunkInfo{
	ChunkInfo() :    firstBegin_(0), firstEnd_(0),
					lastBegin_(0), lastEnd_(0), numAlignedChunks_(0)
	{}
	bool hasFirst(void){
		return firstEnd_ != firstBegin_;
	}
	bool hasLast(void){
		return lastEnd_ != lastBegin_;
	}
	uint32_t numChunks(void){
		uint32_t rc = numAlignedChunks_;
		if (hasFirst())
			rc++;
		if (hasLast())
			rc++;

		return rc;
	}
	uint64_t firstBegin_;
	uint64_t firstEnd_;
	uint64_t lastBegin_;
	uint64_t lastEnd_;
	uint32_t numAlignedChunks_;
};

struct StripChunkerInitInfo{
	StripChunkerInitInfo() : StripChunkerInitInfo(0,0,ImageStripper())
	{}
	StripChunkerInitInfo(uint64_t headerSize, uint64_t writeSize, ImageStripper imageStripper) :
		headerSize_(headerSize), writeSize_(writeSize), imageStripper_(imageStripper)
	{}
	uint64_t headerSize_;
	uint64_t writeSize_;
	ImageStripper imageStripper_;
};

class StripChunker {
public:
	StripChunker(StripChunkerInitInfo initInfo);
	virtual ~StripChunker() = default;
	ChunkInfo getChunkInfo(uint32_t strip);
private:
	uint64_t stripOffset(uint32_t strip);
	uint64_t stripEnd(uint32_t strip);
	uint64_t lastBegin(uint32_t strip);
	ImageStripper& imageStripper(void);
	StripChunkerInitInfo init_;
	uint32_t finalStrip_;
};
