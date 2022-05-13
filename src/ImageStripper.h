#pragma once

#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>

#include "IBufferPool.h"

struct IOBufArray{
	IOBufArray(IOBuf **buffers, uint32_t numBuffers, IBufferPool *pool)
		: buffers_(buffers),
		  numBuffers_(numBuffers),
		  pool_(pool)
	{
		for (uint32_t i = 0; i < numBuffers_; ++i)
			assert(buffers_[i]->data);
	}
	~IOBufArray(void){
		for (uint32_t i = 0; i < numBuffers_; ++i)
			pool_->put(buffers_[i]);
		delete[] buffers_;
	}
	IOBuf ** buffers_;
	uint32_t numBuffers_;
	IBufferPool *pool_;
};

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
 * share a common seam, which happens when the boundary between two strips
 * is not aligned.
 */

struct ChunkInfo{
	ChunkInfo() : ChunkInfo(false,false,0,0,0,0,0,0)
	{}
	ChunkInfo(bool isFirstStrip,
			 bool isFinalStrip,
			 uint64_t logicalOffset,
			 uint64_t logicalLen,
			 uint64_t logicalOffsetPrev,
			 uint64_t logicalLenPrev,
			 uint64_t headerSize,
			 uint64_t writeSize) 	:
				 	 	firstBegin_(0),
						firstEnd_(0),
						lastBegin_(0),
						lastEnd_(0),
						writeSize_(writeSize),
						isFirstStrip_(isFirstStrip),
						isFinalStrip_(isFinalStrip),
						headerSize_(headerSize),
						pool_(nullptr)
	{
		if (!writeSize_)
			return;
		lastBegin_    = lastBegin(logicalOffset,logicalLen);
		assert(lastBegin_% writeSize_ == 0);
		lastEnd_      = stripEnd(logicalOffset,logicalLen);
		firstBegin_   = stripOffset(logicalOffset);
		firstEnd_     =
				(isFirstStrip_ ? 0 :
						lastBegin(logicalOffsetPrev,logicalLenPrev)) + writeSize_;
		assert(firstEnd_% writeSize_ == 0);
		assert((lastBegin_ - firstEnd_) % writeSize_ == 0);
		assert(firstEnd_ >= firstBegin_);
		assert(lastEnd_ >= lastBegin_);
		assert(firstEnd_ <= lastBegin_);
	}
	uint64_t len(){
		return lastEnd_ - firstBegin_;
	}
	uint32_t numChunks(void){
		uint64_t nonSeamBegin = hasFirst() ? firstEnd_ : firstBegin_;
		uint64_t nonSeamEnd   = hasLast() ? lastBegin_ : lastEnd_;
		assert(nonSeamEnd > nonSeamBegin );
		assert(IOBuf::isAlignedToWriteSize(nonSeamBegin));
		assert(isFinalStrip_ || IOBuf::isAlignedToWriteSize(nonSeamEnd));
		uint64_t rc = (nonSeamEnd - nonSeamBegin + writeSize_ - 1) / writeSize_;
		if (hasFirst())
			rc++;
		if (hasLast())
			rc++;
		assert(rc > 1);

		return rc;
	}
	bool hasFirst(void){
		return !isFirstStrip_ && !IOBuf::isAlignedToWriteSize(firstBegin_);
	}
	bool hasLast(void){
		return !isFinalStrip_ && !IOBuf::isAlignedToWriteSize(lastEnd_);
	}
	uint64_t stripOffset(uint64_t logicalOffset){
		// header bytes added to first strip shifts all other strips
		// by that number of bytes
		return isFirstStrip_ ? 0 : headerSize_ + logicalOffset;
	}
	uint64_t stripEnd(uint64_t logicalOffset, uint64_t logicalLen){
		uint64_t rc = stripOffset(logicalOffset) + logicalLen;
		//correct for header bytes added to first strip
		if (isFirstStrip_)
			rc += headerSize_;

		return rc;
	}
	uint64_t lastBegin(uint64_t logicalOffset,uint64_t logicalLen){
		return (stripEnd(logicalOffset,logicalLen)/writeSize_) * writeSize_;
	}
	ChunkInfo(const ChunkInfo &rhs){
		firstBegin_ = rhs.firstBegin_;
		firstEnd_ = rhs.firstEnd_;
		lastBegin_ = rhs.lastBegin_;
		lastEnd_ = rhs.lastEnd_;
		writeSize_= rhs.writeSize_;
		isFirstStrip_ = rhs.isFinalStrip_;
		isFinalStrip_ = rhs.isFinalStrip_;
		headerSize_ = rhs.headerSize_;
		pool_ = rhs.pool_;
	}
	uint64_t firstBegin_;
	uint64_t firstEnd_;
	uint64_t lastBegin_;
	uint64_t lastEnd_;
	uint64_t writeSize_;
	bool isFirstStrip_;
	bool isFinalStrip_;
	uint64_t headerSize_;
	IBufferPool *pool_;
};
struct IOChunk{
	IOChunk(uint64_t offset,uint64_t len, IBufferPool *pool) :
		offset_(offset),
		len_(len),
		buf_(nullptr),
		refCount_(1),
		writeCount_(0),
		writeTarget_(1)
	{
		if (pool)
			alloc(pool);
	}
	IOChunk* ref(void){
		++refCount_;
		return this;
	}
	uint32_t unref(void){
		return --refCount_;
	}
	bool acquire(void){
		 return (++writeCount_ == writeTarget_);
	}
	void alloc(IBufferPool* pool){
		assert(pool);
		assert(!buf_ || !buf_->data);
		buf_ = pool->get(len_);
		buf_->offset = offset_;
	}
	uint64_t offset_;
	uint64_t len_;
	IOBuf *buf_;
	std::atomic<uint32_t> refCount_;
	std::atomic<uint32_t> writeCount_;
	uint32_t writeTarget_;
};

/**
 * A strip chunk wraps a serialize chunk (which may be shared with the
 * strip's neighbour), and it also stores a write offset and write length
 * for its assigned portion of a shared serialize chunk.
 * If there is no sharing, then  write offset is zero,
 * and write length equals WRTSIZE.
 */
struct StripChunk {
	StripChunk(IOChunk *ioChunk,
				uint64_t writeableOffset,
				uint64_t writeableLen,
				bool shared) :
		ioChunk_(ioChunk),
		writeableOffset_(writeableOffset),
		writeableLen_(writeableLen),
		shared_(shared)
	{
		assert(writeableOffset < len());
		assert(writeableLen <= len());
	}
	~StripChunk(){
		if (unref() == 0)
			delete ioChunk_;
	}
	void alloc(IBufferPool* pool){
		if (!shared_)
			ioChunk_->alloc(pool);
	}
	uint64_t offset(void){
		return ioChunk_->offset_;
	}
	uint64_t len(void){
		return ioChunk_->len_;
	}
	IOChunk* ref(void){
		return ioChunk_->ref();
	}
	uint32_t unref(void){
		return ioChunk_->unref();
	}
	bool acquire(void){
		return ioChunk_->acquire();
	}
	uint8_t* data(void){
		return ioChunk_->buf_->data;
	}
	void setHeader(uint8_t *headerData, uint64_t headerSize){
		memcpy(data() , headerData, headerSize);
		ioChunk_->buf_->skip = headerSize;
		writeableOffset_ = headerSize;
	}
	// write offset relative to beginning of data
	uint64_t writeableOffset_;
	// writeable length
	uint64_t writeableLen_;
	bool shared_;
	IOChunk *ioChunk_;
};

/**
 * A strip contains an array of strip chunks
 *
 */
struct Strip  {
	Strip(uint64_t offset,uint64_t len,Strip* neighbour) :
		offset_(offset),
		len_(len),
		chunks_(nullptr),
		numChunks_(0),
		leftNeighbour_(neighbour)
	{}
	~Strip(void){
		if (chunks_){
			for (uint32_t i = 0; i < numChunks_; ++i)
				delete chunks_[i];
			delete[] chunks_;
		}
	}
	void generateChunks(ChunkInfo chunkInfo, IBufferPool *pool){
		chunkInfo_ = chunkInfo;
		numChunks_ = chunkInfo_.numChunks();
		assert(numChunks_);
		chunks_ = new StripChunk*[numChunks_];
		uint64_t writeableTotal = 0;
		for (uint32_t i = 0; i < numChunks_; ++i ){
			uint64_t off = (chunkInfo.firstEnd_ - chunkInfo_.writeSize_) +
								i * chunkInfo_.writeSize_;
			bool lastChunkOfAll  = chunkInfo.isFinalStrip_ && (i == numChunks_-1);
			uint64_t len = lastChunkOfAll ?
							(chunkInfo_.lastEnd_ - chunkInfo_.lastBegin_) :
									chunkInfo_.writeSize_;
			uint64_t writeableOffset = 0;
			uint64_t writeableLen = len;
			bool shared = false;
			bool firstSeam       = (i == 0) && chunkInfo.hasFirst();
			bool lastSeam        = (i == numChunks_-1) && !lastChunkOfAll && chunkInfo.hasLast();
			if (firstSeam){
				assert(leftNeighbour_);
				off = leftNeighbour_->finalChunk()->offset();
				assert(chunkInfo_.firstBegin_  > off);
				writeableOffset = chunkInfo_.firstBegin_ - off;
				writeableLen    = chunkInfo_.firstEnd_ - chunkInfo_.firstBegin_;
				assert(writeableLen && writeableLen < chunkInfo_.writeSize_);
				assert(writeableOffset && writeableOffset < chunkInfo_.writeSize_);
				shared = true;
			} else if (lastSeam){
				off   = chunkInfo_.lastBegin_;
				writeableLen = chunkInfo_.lastEnd_ - chunkInfo_.lastBegin_;
				assert(writeableLen && writeableLen < chunkInfo_.writeSize_);
				if (lastChunkOfAll)
					len = writeableLen;
				else
					shared = true;
			} else if (chunkInfo_.isFirstStrip_ && i == 0){
				// first chunk of first strip
				writeableOffset += chunkInfo_.headerSize_;
				writeableLen -= chunkInfo_.headerSize_;
			}
			assert(!lastSeam || !firstSeam);
			writeableTotal += writeableLen;
			assert(IOBuf::isAlignedToWriteSize(off));
			assert(lastChunkOfAll || IOBuf::isAlignedToWriteSize(len));
			IOChunk* ioChunk = nullptr;
			if (firstSeam){
				ioChunk = leftNeighbour_->finalChunk()->ref();
				assert(ioChunk->buf_->data);
			}
			else {
				ioChunk = new IOChunk(off,len,(shared ? pool : nullptr));
			}
			chunks_[i] = new StripChunk(ioChunk,
										writeableOffset,
										writeableLen,
										shared);
			assert(!shared || chunks_[i]->ioChunk_->buf_->data);
		}
		uint64_t stripWriteEnd = chunks_[numChunks_-1]->offset() +
				+ chunks_[numChunks_-1]->writeableLen_;
		assert(stripWriteEnd == chunkInfo_.lastEnd_);
		assert(!chunkInfo.isFirstStrip_ || chunks_[0]->offset() == 0);

		uint64_t stripWriteBegin = chunks_[0]->offset() + chunks_[0]->writeableOffset_;
		assert(stripWriteBegin == chunkInfo_.firstBegin_ + (chunkInfo_.isFirstStrip_ ? chunkInfo_.headerSize_ : 0));

		assert(stripWriteEnd - stripWriteBegin == len_);
		assert(len_ == writeableTotal);
	}
	IOBufArray* genBufferArray(IBufferPool *pool, uint8_t *header, uint64_t headerLen){
		auto first = chunks_[0];
		bool acquiredFirst = true;
		if (first->shared_)
			acquiredFirst = first->acquire();
		uint32_t numBuffers = numChunks_ - (acquiredFirst ? 0 : 1);
		auto ret = new IOBuf*[numBuffers];
		uint32_t count = 0;
		for (uint32_t i = 0; i < numChunks_; ++i){
			if (!acquiredFirst)
				continue;
			auto ch = chunks_[i];
			assert(!ch->shared_ || ch->ioChunk_->buf_->data);
			ch->alloc(pool);
			assert(ch->ioChunk_->buf_->data);
			if (header)
				ch->setHeader(header, headerLen);
			ret[count++] = ch->ioChunk_->buf_;
		}
		for (uint32_t i = 0; i < numBuffers; ++i){
			assert(ret[i]->data);
			assert(ret[i]->dataLen);
		}
		assert(count == numBuffers);

		return new IOBufArray(ret,numBuffers,pool);
	}
	StripChunk* finalChunk(void){
		return chunks_[numChunks_-1];
	}

	// independant of header size
	uint64_t offset_;
	uint64_t len_;
	////////////////

	StripChunk** chunks_;
	uint32_t numChunks_;
	Strip *leftNeighbour_;
	ChunkInfo chunkInfo_;
};

/*
 * Divide an image into strips
 *
 */
struct ImageStripper{
	ImageStripper(uint32_t width,
				uint32_t height,
				uint16_t numcomps,
				uint32_t nominalStripHeight,
				uint64_t headerSize,
				uint64_t writeSize,
				IBufferPool *pool) :
		width_(width),
		height_(height),
		numcomps_(numcomps),
		nominalStripHeight_(nominalStripHeight),
		numStrips_(nominalStripHeight ?
				(height  + nominalStripHeight - 1)/ nominalStripHeight : 0),
		stripPackedByteWidth_(numcomps * width),
		stripLen_(nominalStripHeight * stripPackedByteWidth_),
		finalStripHeight_((nominalStripHeight && (height % nominalStripHeight != 0)) ?
							height - ((height / nominalStripHeight) * nominalStripHeight) :
								nominalStripHeight),
		finalStripLen_ (finalStripHeight_ * stripPackedByteWidth_),
		headerSize_(headerSize),
		writeSize_(writeSize),
		finalStrip_(numStrips_-1),
		stripBufs_(new Strip*[numStrips_])
	{
		for (uint32_t i = 0; i < numStrips_; ++i){
			auto neighbour = (i > 0) ? stripBufs_[i-1] : nullptr;
			stripBufs_[i] =
					new Strip(i * nominalStripHeight_ * stripPackedByteWidth_,
								stripHeight(i) * stripPackedByteWidth_,
								neighbour);
			if (pool)
				stripBufs_[i]->generateChunks(getChunkInfo(i), pool);
		}
	}
	~ImageStripper(void){
		if (stripBufs_){
			for (uint32_t i = 0; i < numStrips_; ++i)
				delete stripBufs_[i];
			delete[] stripBufs_;
		}
	}
	Strip* getStrip(uint32_t strip) const{
		return stripBufs_[strip];
	}
	uint32_t numStrips(void) const{
		return numStrips_;
	}
	ChunkInfo getChunkInfo(uint32_t strip){
		return ChunkInfo(strip == 0,
						strip == finalStrip_,
						stripBufs_[strip]->offset_,
						stripBufs_[strip]->len_,
						strip == 0 ? 0 : stripBufs_[strip-1]->offset_,
						strip == 0 ? 0 : stripBufs_[strip-1]->len_,
						headerSize_,
						writeSize_);
	}
	uint32_t width_;
	uint32_t height_;
	uint16_t numcomps_;
	uint32_t nominalStripHeight_;
private:
	uint32_t stripHeight(uint32_t strip) const{
		return (strip < numStrips_-1) ? nominalStripHeight_ : finalStripHeight_;
	}
	uint64_t stripOffsetHeaderCorrected(uint32_t strip){
		// header bytes added to first strip shifts all other strips
		// by that number of bytes
		return strip == 0 ? 0 : headerSize_ + stripBufs_[strip]->offset_;
	}
	uint64_t stripEndHeaderCorrected(uint32_t strip){
		uint64_t rc = stripOffsetHeaderCorrected(strip) + stripBufs_[strip]->len_;
		//correct for header bytes added to first strip
		if (strip == 0)
			rc += headerSize_;

		return rc;
	}
	uint64_t lastBeginHeaderCorrected(uint32_t strip){
		return (stripEndHeaderCorrected(strip)/writeSize_) * writeSize_;
	}
	uint64_t stripPackedByteWidth_;
	uint64_t stripLen_;
	uint32_t finalStripHeight_;
	uint64_t finalStripLen_;
	uint32_t numStrips_;
	uint64_t headerSize_;
	uint64_t writeSize_;
	uint32_t finalStrip_;
	Strip **stripBufs_;
};
