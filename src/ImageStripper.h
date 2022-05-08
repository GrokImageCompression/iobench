#pragma once

#include <cstdint>
#include <atomic>
#include <thread>

#include "IBufferPool.h"
#include <mutex>

/*
 * Each strip is divided into a collection of serialize chunks,
 * which are designed for serialization to disk. Their offset is always
 * aligned, and their length is always equal to WRTSIZE (4k),
 * except possibly the final chunk of the final strip. Also, they are corrected
 * for the header bytes which are located right before the beginning of the
 * first strip - the header bytes are included in the first chunk of the first
 * strip.
 *
 * Serialize chunks can be shared between neighbouring strips if they
 * share a common seam, which happens when the strip's corrected offset
 * is not aligned.
 */

struct SerializeChunkInfo{
	SerializeChunkInfo(void) :    firstBegin_(0), firstEnd_(0),
					lastBegin_(0), lastEnd_(0), numWholeChunks_(0),
					writeSize_(0), isFirstStrip_(false),
					isFinalStrip_(false)
	{}
	uint64_t len(){
		return lastEnd_ - firstBegin_;
	}
	uint32_t numChunks(void){
		uint32_t rc = numWholeChunks_;
		if (hasFirst())
			rc++;
		if (hasLast())
			rc++;

		return rc;
	}
	bool hasFirst(void){
		return !isFirstStrip_ && !SerializeBuf::isAligned(firstBegin_);
	}
	bool hasLast(void){
		return !isFinalStrip_ && !SerializeBuf::isAligned(lastEnd_);
	}
	uint64_t firstBegin_;
	uint64_t firstEnd_;
	uint64_t lastBegin_;
	uint64_t lastEnd_;
	uint32_t numWholeChunks_;
	uint64_t writeSize_;
	bool isFirstStrip_;
	bool isFinalStrip_;
};
struct SerializeChunkBuffer{
	SerializeChunkBuffer(uint64_t offset,uint64_t len, bool shared, bool finalChunk) :
		refCount_(1),writeCount_(0), writeTarget_(1),
		shared_(shared)
	{
		buf_.offset = offset;
		buf_.dataLen = len;
		assert(buf_.alignedOffset());
		assert(finalChunk || buf_.alignedLength());
	}
	uint32_t ref(void){
		return ++refCount_;
	}
	uint32_t unref(void){
		return --refCount_;
	}
	bool submit(ISerializeBufWriter *writer){
		assert(writer);
		if (++writeCount_ == writeTarget_)
			return writer->write(buf_) == buf_.dataLen;
		return true;
	}
	void alloc(IBufferPool* pool){
		assert(pool);
		if (shared_){
			std::unique_lock<std::mutex> locker(sharedMut_);
			allocInternal(pool);
		} else {
			allocInternal(pool);
		}
	}
	SerializeBuf buf_;
	std::atomic<uint32_t> refCount_;
	std::atomic<uint32_t> writeCount_;
	uint32_t writeTarget_;
	bool shared_;
	std::mutex sharedMut_;
private:
	void allocInternal(IBufferPool* pool){
		assert(pool);
		if (buf_.data)
			return;
		// cache offset
		uint64_t offset = buf_.offset;
		// allocate
		buf_ = pool->get(buf_.allocLen);
		// restore offset
		buf_.offset = offset;
	}
};

/**
 * A strip chunk wraps a serialize chunk (which may be shared with the
 * strip's neighbour), and it also stores a write offset and write length
 * for its assigned portion of a shared serialize chunk.
 * If there is no sharing, then  write offset is zero,
 * and write length equals WRTSIZE.
 */
struct StripChunkBuffer {
	StripChunkBuffer(SerializeChunkBuffer *serializeChunkBuffer,
			uint64_t writeOffset, uint64_t writeLen) :
		serializeChunkBuffer_(serializeChunkBuffer),
		writeOffset_(writeOffset),
		writeLen_(writeLen)
	{
		assert(writeOffset < serializeChunkBuffer_->buf_.dataLen);
		assert(writeLen <= serializeChunkBuffer_->buf_.dataLen);
	}
	~StripChunkBuffer(){
		if (serializeChunkBuffer_->unref() == 0)
			delete serializeChunkBuffer_;
	}
	void alloc(IBufferPool* pool){
		serializeChunkBuffer_->alloc(pool);
	}
	SerializeChunkBuffer *serializeChunkBuffer_;
	// write offset relative to beginning of data
	uint64_t writeOffset_;
	// writeable length
	uint64_t writeLen_;
};

/**
 * A strip buffer contains a sequence of strip chunks, and is able to iterate
 * through this sequence, feeding strip chunks to be consumed by the caller.
 *
 */
struct StripBuffer  {
	StripBuffer(uint64_t offset,uint64_t len,StripBuffer* neighbour) :
		offset_(offset),
		len_(len),
		chunks_(nullptr),
		numChunks_(0),
		nextChunkIndex_(-1),
		leftNeighbour_(neighbour)
	{}
	~StripBuffer(void){
		if (chunks_){
			for (uint32_t i = 0; i < numChunks_; ++i)
				delete chunks_[i];
			delete[] chunks_;
		}
	}
	void init(SerializeChunkInfo chunkInfo){
		chunkInfo_ = chunkInfo;
		numChunks_ = chunkInfo_.numChunks();
		chunks_ = new StripChunkBuffer*[numChunks_];
		for (uint32_t i = 0; i < numChunks_; ++i ){
			bool lastChunkOfAll  = chunkInfo.isFinalStrip_ && (i == numChunks_-1);
			bool firstSeam   = i == 0 && chunkInfo.hasFirst();
			bool lastSeam    = !lastChunkOfAll && (i == numChunks_-1) && chunkInfo.hasLast();
			uint64_t offset  = (i == 0) ? chunkInfo_.firstBegin_ :
									chunkInfo_.firstEnd_ + (i-1) * chunkInfo_.writeSize_;
			uint64_t len     = chunkInfo_.writeSize_;
			uint64_t writeOffset = 0;
			uint64_t writeLen = len;
			bool shared = false;
			if (firstSeam){
				assert(leftNeighbour_);
				offset = leftNeighbour_->finalChunk()->serializeChunkBuffer_->buf_.offset;
				assert(chunkInfo_.firstBegin_  > offset);
				writeOffset = chunkInfo_.firstBegin_ - offset;
				writeLen = chunkInfo_.firstEnd_ - chunkInfo_.firstBegin_;
				assert(writeLen);
				shared = true;
			} else if (lastSeam){
				offset = chunkInfo_.lastBegin_;
				writeLen = chunkInfo_.lastEnd_ - chunkInfo_.lastBegin_;
				if (lastChunkOfAll)
					len = writeLen;
				else
					shared = true;
			} else 	if (chunkInfo.hasFirst()) {
				offset = chunkInfo_.firstEnd_ + (i-1) * chunkInfo_.writeSize_;
			}
			SerializeChunkBuffer* serializeChunkBuffer;
			if (firstSeam) {
				serializeChunkBuffer = leftNeighbour_->finalChunk()->serializeChunkBuffer_;
				serializeChunkBuffer->ref();
			} else {
				serializeChunkBuffer = new SerializeChunkBuffer(offset,len,shared,lastChunkOfAll);
			}
			chunks_[i] = new StripChunkBuffer(serializeChunkBuffer,
												writeOffset,
												writeLen);
		}
	}
	bool nextChunk(IBufferPool *pool, StripChunkBuffer **chunkBuffer){
		assert(pool);
		assert(chunkBuffer);
		if (nextChunkIndex_ >= numChunks_)
			return false;
		uint32_t chunk = ++nextChunkIndex_;
		if (chunk >= numChunks_)
			return false;
		chunks_[chunk]->alloc(pool);
		*chunkBuffer = chunks_[chunk];

		return true;
	}
	StripChunkBuffer* finalChunk(void){
		return chunks_[numChunks_-1];
	}

	// temporary
	uint64_t offset_;
	uint64_t len_;
	////////////////

	StripChunkBuffer** chunks_;
	uint32_t numChunks_;
	std::atomic<int32_t> nextChunkIndex_;
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
			stripBuffers_[i] = new StripBuffer(i * nominalStripHeight_ * stripPackedByteWidth_,
										stripHeight(i) * stripPackedByteWidth_,
										neighbour);
			stripBuffers_[i]->init(getSerializeChunkInfo(i));
		}
	}
	StripBuffer* getStrip(uint32_t strip) const{
		return stripBuffers_[strip];
	}
	uint32_t numStrips(void) const{
		return numStrips_;
	}
	SerializeChunkInfo getSerializeChunkInfo(uint32_t strip){
		SerializeChunkInfo ret;
		ret.isFirstStrip_ = strip == 0;
		ret.isFinalStrip_ = strip == finalStrip_;
		ret.writeSize_    = writeSize_;
		ret.lastBegin_    = correctedLastBegin(strip);
		assert(ret.isFinalStrip_ || (ret.lastBegin_% writeSize_ == 0));
		ret.lastEnd_      = correctedStripEnd(strip);
		ret.firstBegin_   = correctedStripOffset(strip);
		ret.firstEnd_     = (strip == 0 ? 0 : correctedLastBegin(strip-1)) + writeSize_;
		assert(ret.firstEnd_% writeSize_ == 0);
		assert(ret.isFinalStrip_||
				((ret.lastBegin_ - ret.firstEnd_) % writeSize_ == 0) );
		ret.numWholeChunks_ = (ret.lastBegin_ - ret.firstEnd_) / writeSize_;

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
	uint64_t correctedLastBegin(uint32_t strip){
		return (correctedStripEnd(strip)/writeSize_) * writeSize_;
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
