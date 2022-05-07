#include <StripChunker.h>
#include <cassert>

/**
 * Break strip into chunks. Note: header bytes at beginning of
 * first chunk is taken into account.
 *
 */
StripChunker::StripChunker(StripChunkerInitInfo initInfo) : init_(initInfo),
										finalStrip_(init_.imageStripper_.numStrips()-1)
{}
ChunkInfo StripChunker::getChunkInfo(uint32_t strip){
	ChunkInfo ret;
	ret.lastBegin_ = lastBegin(strip);
	assert(strip ==  finalStrip_ || (ret.lastBegin_% init_.writeSize_ == 0));
	ret.lastEnd_   = stripEnd(strip);
	ret.firstBegin_ = stripOffset(strip);
	// no lower seam
	if (strip == 0 ||  (ret.firstBegin_ % init_.writeSize_ == 0))
		ret.firstEnd_ = ret.firstBegin_;
	else
		ret.firstEnd_ = lastBegin(strip-1) + init_.writeSize_;
	assert(ret.firstEnd_% init_.writeSize_ == 0);
	assert((strip ==  finalStrip_) ||
			((ret.lastBegin_ - ret.firstEnd_) % init_.writeSize_ == 0) );
	ret.numAlignedChunks_ = (ret.lastBegin_ - ret.firstEnd_) / init_.writeSize_;

	return ret;
}
ImageStripper& StripChunker::imageStripper(void){
	return init_.imageStripper_;
}
uint64_t StripChunker::stripOffset(uint32_t strip){
	// header bytes added to first strip shifts all other strips
	// by that number of bytes
	return strip == 0 ? 0 : init_.headerSize_ + imageStripper().getStrip(strip).offset_;
}
uint64_t StripChunker::stripEnd(uint32_t strip){
	uint64_t rc = stripOffset(strip) + imageStripper().getStrip(strip).len_;
	//correct for header bytes added to first strip
	if (strip == 0)
		rc += init_.headerSize_;

	return rc;
}
uint64_t StripChunker::lastBegin(uint32_t strip){
	return (strip < finalStrip_) ?
			(stripEnd(strip)/init_.writeSize_) * init_.writeSize_ : stripEnd(strip);
}
