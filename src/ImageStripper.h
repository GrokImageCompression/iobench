#pragma once

#include <cstdint>
#include <atomic>
#include <thread>

#include "IBufferPool.h"

struct Chunk {
	Chunk(void) : Chunk(0,0,0)
	{}
	Chunk(uint64_t offsetForWrite, uint64_t offset,uint64_t len) :
		offsetForWrite_(offsetForWrite), offset_(offset), len_(len)
	{}
	Chunk(uint64_t offset,uint64_t len) :Chunk(offset,offset,len)
	{}
	bool aligned(void){
		return (offset_ & (ALIGNMENT-1)) && (len_ & (ALIGNMENT-1));
	}
	uint64_t offsetForWrite_;
	uint64_t offset_;
	uint64_t len_;
};

struct ChunkBuffer : public Chunk{
	ChunkBuffer() : ChunkBuffer(0,0,0,0)
	{}
	ChunkBuffer(uint64_t offsetForWrite, uint64_t offset,uint64_t len, uint64_t allocLen) :
		Chunk(offsetForWrite,offset,len),
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

struct StripBuffer : public Chunk {
	StripBuffer(uint64_t offset,uint64_t len) :
		Chunk(offset,offset,len),
		chunks_(nullptr), numChunks_(0), nextChunkIndex_(-1)
	{}
	bool nextChunk(IBufferPool *pool, ChunkBuffer &chunkBuffer){
		int32_t ind = ++nextChunkIndex_;
		if (ind >= numChunks_)
			return false;

		chunkBuffer = chunks_[ind];
		if (chunkBuffer.aligned()){
			chunkBuffer.alloc(pool);
		} else {
			if (ind == 0){

			} else {

			}
		}

		return true;
	}
	ChunkBuffer* chunks_;
	uint32_t numChunks_;
	std::atomic<int32_t> nextChunkIndex_;
	std::mutex seamMutex_;
};

struct ImageStripper{
	ImageStripper() : ImageStripper(0,0,0,0)
	{}
	ImageStripper(uint32_t width, uint32_t height, uint16_t numcomps, uint32_t nominalStripHeight) :
		width_(width), height_(height),numcomps_(numcomps), nominalStripHeight_(nominalStripHeight),
		numStrips_(nominalStripHeight ? (height  + nominalStripHeight - 1)/ nominalStripHeight : 0),
		stripPackedByteWidth_(numcomps * width),
		stripLen_(nominalStripHeight * stripPackedByteWidth_),
		finalStripHeight_( (nominalStripHeight && (height % nominalStripHeight != 0)) ?
							height - ((height / nominalStripHeight) * nominalStripHeight) :
								nominalStripHeight),
		finalStripLen_ (finalStripHeight_ * stripPackedByteWidth_)
	{}
	StripBuffer getStrip(uint32_t strip) const{
		return StripBuffer(strip * nominalStripHeight_ * stripPackedByteWidth_,
					stripHeight(strip) * stripPackedByteWidth_);
	}
	uint32_t numStrips(void) const{
		return numStrips_;
	}
	uint32_t width_;
	uint32_t height_;
	uint16_t numcomps_;
	uint32_t nominalStripHeight_;
private:
	uint32_t stripHeight(uint32_t strip) const{
		return (strip < numStrips_-1) ? nominalStripHeight_ : finalStripHeight_;
	}
	uint64_t stripPackedByteWidth_;
	uint64_t stripLen_;
	uint32_t finalStripHeight_;
	uint64_t finalStripLen_;
	uint32_t numStrips_;
	StripBuffer *stripBuffers_;
};
