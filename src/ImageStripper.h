#pragma once

#include <cstdint>
#include <atomic>
#include <thread>

#include "IBufferPool.h"

// corrected for header size
struct SerializeChunkInfo{
	SerializeChunkInfo() :    firstBegin_(0), firstEnd_(0),
					lastBegin_(0), lastEnd_(0), numAlignedChunks_(0)
	{}
	uint64_t len(){
		return lastEnd_ - firstBegin_;
	}
	uint64_t firstBegin_;
	uint64_t firstEnd_;
	uint64_t lastBegin_;
	uint64_t lastEnd_;
	uint32_t numAlignedChunks_;
private:
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
};
struct SerializeChunk {
	SerializeChunk(uint64_t offsetForWrite, uint64_t offset,uint64_t len) :
		offsetForWrite_(offsetForWrite), offset_(offset), len_(len)
	{}
	SerializeChunk(uint64_t offset,uint64_t len) :SerializeChunk(offset,offset,len)
	{}
	bool aligned(void){
		return (offset_ & (ALIGNMENT-1)) && (len_ & (ALIGNMENT-1));
	}
	uint64_t offsetForWrite_;
	uint64_t offset_;
	uint64_t len_;
};
struct SerializeChunkBuffer : public SerializeChunk{
	SerializeChunkBuffer(uint64_t offsetForWrite, uint64_t offset,uint64_t len, uint64_t allocLen) :
		SerializeChunk(offsetForWrite,offset,len),
		data_(nullptr), allocLen_(allocLen), pool_(nullptr)
	{}
	void alloc(IBufferPool* pool){
		auto b = pool->get(len_);
		data_ = b.data;
		allocLen_ = b.allocLen;
		pool_ = pool;
	}
	void reclaim(void){
		if (pool_ && data_) {
			pool_->put(SerializeBuf(data_,allocLen_));
			data_ = nullptr;
			allocLen_ = 0;
			pool_ = nullptr;
		}
	}
	uint8_t* data_;
	uint64_t allocLen_;
	IBufferPool* pool_;
};

// independant of header size
struct StripBuffer  {
	StripBuffer(StripBuffer* neighbour, uint64_t offset,uint64_t len) :
		offset_(offset),
		len_(len),
		chunks_(nullptr),
		numChunks_(0),
		nextChunkIndex_(-1),
		leftNeighbour_(neighbour)
	{}
	void setSerializeChunkInfo(SerializeChunkInfo chunkInfo){
		chunkInfo_ = chunkInfo;
	}
	bool nextChunk(IBufferPool *pool, SerializeChunkBuffer &chunkBuffer){
		int32_t ind = ++nextChunkIndex_;
		if (ind >= numChunks_)
			return false;

		chunkBuffer = chunks_[ind];
		if (chunkBuffer.aligned()){
			chunkBuffer.alloc(pool);
		} else {
			if (ind == 0){
				assert(leftNeighbour_);
				// get left neighbour last chunk
			} else {

			}
		}

		return true;
	}

	uint64_t offset_;
	uint64_t len_;
	SerializeChunkBuffer* chunks_;
	uint32_t numChunks_;
	std::atomic<int32_t> nextChunkIndex_;
	std::mutex seamMutex_;
	StripBuffer *leftNeighbour_;
	SerializeChunkInfo chunkInfo_;
};


struct ImageStripper{
	ImageStripper() : ImageStripper(0,0,0,0,0,0)
	{}
	~ImageStripper(void){
		if (stripBuffers_){
			for (uint32_t i = 0; i < numStrips_; ++i)
				delete stripBuffers_[i];
			delete[] stripBuffers_;
		}
	}
	ImageStripper(uint32_t width, uint32_t height, uint16_t numcomps,
					uint32_t nominalStripHeight, uint64_t headerSize, uint64_t writeSize) :
		width_(width), height_(height),numcomps_(numcomps), nominalStripHeight_(nominalStripHeight),
		numStrips_(nominalStripHeight ? (height  + nominalStripHeight - 1)/ nominalStripHeight : 0),
		stripPackedByteWidth_(numcomps * width),
		stripLen_(nominalStripHeight * stripPackedByteWidth_),
		finalStripHeight_( (nominalStripHeight && (height % nominalStripHeight != 0)) ?
							height - ((height / nominalStripHeight) * nominalStripHeight) :
								nominalStripHeight),
		finalStripLen_ (finalStripHeight_ * stripPackedByteWidth_),
		headerSize_(headerSize),
		writeSize_(writeSize),
		finalStrip_(numStrips_-1),
		stripBuffers_(new StripBuffer*[numStrips_])
	{
		for (uint32_t i = 0; i < numStrips_; ++i){
			auto neighbour = (i > 0) ? stripBuffers_[i-1] : nullptr;
			stripBuffers_[i] = new StripBuffer(neighbour,
										i * nominalStripHeight_ * stripPackedByteWidth_,
										stripHeight(i) * stripPackedByteWidth_);
			stripBuffers_[i]->setSerializeChunkInfo(getSerializeChunkInfo(i));
		}
	}
	StripBuffer* getStrip(uint32_t strip) const{
		return stripBuffers_[strip];
	}
	uint32_t numStrips(void) const{
		return numStrips_;
	}
	// corrected
	SerializeChunkInfo getSerializeChunkInfo(uint32_t strip){
		SerializeChunkInfo ret;
		ret.lastBegin_ = lastBegin(strip);
		assert(strip ==  finalStrip_ || (ret.lastBegin_% writeSize_ == 0));
		ret.lastEnd_   = correctedStripEnd(strip);
		ret.firstBegin_ = correctedStripOffset(strip);
		// no lower seam
		if (strip == 0 ||  (ret.firstBegin_ % writeSize_ == 0))
			ret.firstEnd_ = ret.firstBegin_;
		else
			ret.firstEnd_ = lastBegin(strip-1) + writeSize_;
		assert(ret.firstEnd_% writeSize_ == 0);
		assert((strip ==  finalStrip_) ||
				((ret.lastBegin_ - ret.firstEnd_) % writeSize_ == 0) );
		ret.numAlignedChunks_ = (ret.lastBegin_ - ret.firstEnd_) / writeSize_;

		return ret;
	}
	uint32_t width_;
	uint32_t height_;
	uint16_t numcomps_;
	uint32_t nominalStripHeight_;
private:
	uint32_t stripHeight(uint32_t strip) const{
		return (strip < numStrips_-1) ? nominalStripHeight_ : finalStripHeight_;
	}
	uint64_t correctedStripOffset(uint32_t strip){
		// header bytes added to first strip shifts all other strips
		// by that number of bytes
		return strip == 0 ? 0 : headerSize_ + stripBuffers_[strip]->offset_;
	}
	uint64_t correctedStripEnd(uint32_t strip){
		uint64_t rc = correctedStripOffset(strip) + stripBuffers_[strip]->len_;
		//correct for header bytes added to first strip
		if (strip == 0)
			rc += headerSize_;

		return rc;
	}
	uint64_t lastBegin(uint32_t strip){
		return (strip < finalStrip_) ?
				(correctedStripEnd(strip)/writeSize_) * writeSize_ : correctedStripEnd(strip);
	}
	uint64_t stripPackedByteWidth_;
	uint64_t stripLen_;
	uint32_t finalStripHeight_;
	uint64_t finalStripLen_;
	uint32_t numStrips_;
	uint64_t headerSize_;
	uint64_t writeSize_;
	uint32_t finalStrip_;
	StripBuffer **stripBuffers_;
};
