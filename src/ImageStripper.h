#pragma once

#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>

#include "IBufferPool.h"
#include "RefCounted.h"
#include "util.h"

/*
 * Each strip is divided into a collection of IOChunks, and
 * each IOChunk contains an IOBuf, which is designed for disk IO.
 * An IOBuf's offset is always aligned, and their length is always equal to WRTSIZE,
 * except possibly the final IOBuf of the final strip. Also, they are corrected
 * for the header bytes which are located right before the beginning of the
 * first strip - the header bytes are included in the first IOBuf of the first
 * strip.
 *
 * IOChunks can be shared between neighbouring strips if they
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
						writeSize_(writeSize),
						isFirstStrip_(isFirstStrip),
						isFinalStrip_(isFinalStrip),
						headerSize_(headerSize),
						pool_(nullptr)
	{
		if (!writeSize_)
			return;
		last_.x0_      = lastBegin(logicalOffset,logicalLen);
		assert(IOBuf::isAlignedToWriteSize(last_.x0_));
		last_.x1_      = stripEnd(logicalOffset,logicalLen);
		first_.x0_     = stripOffset(logicalOffset);
		first_.x1_     =
				(isFirstStrip_ ? 0 :
						lastBegin(logicalOffsetPrev,logicalLenPrev)) + writeSize_;
		// clamp first_ end to physical strip length
		first_.x1_ = std::min(first_.x1_,last_.x1_);
		bool firstOverlapsLast = first_.x1_ == last_.x1_;
		assert(firstOverlapsLast || (last_.x0_ - first_.x1_) % writeSize_ == 0);
		assert(first_.valid());
		assert(last_.valid());
		assert(firstOverlapsLast || (first_.x1_ <= last_.x0_));
	}
	uint64_t len(){
		return last_.x1_ - first_.x0_;
	}
	uint32_t numChunks(void){
		bool firstEqualsLast = first_.x1_ == last_.x1_;
		if (firstEqualsLast)
			return 1;
		uint64_t nonSeamBegin = hasFirstSeam() ? first_.x1_ : first_.x0_;
		uint64_t nonSeamEnd   = hasLastSeam() ? last_.x0_ : last_.x1_;
		assert(nonSeamEnd > nonSeamBegin );
		assert(IOBuf::isAlignedToWriteSize(nonSeamBegin));
		assert(isFinalStrip_ || IOBuf::isAlignedToWriteSize(nonSeamEnd));
		uint64_t rc = (nonSeamEnd - nonSeamBegin + writeSize_ - 1) / writeSize_;
		if (hasFirstSeam())
			rc++;
		if (hasLastSeam())
			rc++;
		assert(rc > 1);

		return rc;
	}
	bool hasFirstSeam(void){
		return !isFirstStrip_ && !IOBuf::isAlignedToWriteSize(first_.x0_);
	}
	bool hasLastSeam(void){
		return !isFinalStrip_ && !IOBuf::isAlignedToWriteSize(last_.x1_);
	}
	// not usually aligned
	uint64_t stripOffset(uint64_t logicalOffset){
		// header bytes added to first strip shifts all other strips
		// by that number of bytes
		return isFirstStrip_ ? 0 : headerSize_ + logicalOffset;
	}
	// not usually aligned
	uint64_t stripEnd(uint64_t logicalOffset, uint64_t logicalLen){
		uint64_t rc = stripOffset(logicalOffset) + logicalLen;
		//correct for header bytes added to first strip
		if (isFirstStrip_)
			rc += headerSize_;

		return rc;
	}
	// always aligned
	uint64_t lastBegin(uint64_t logicalOffset,uint64_t logicalLen){
		return (stripEnd(logicalOffset,logicalLen)/writeSize_) * writeSize_;
	}
	ChunkInfo(const ChunkInfo &rhs){
		first_ = rhs.first_;
		last_  = rhs.last_;
		writeSize_= rhs.writeSize_;
		isFirstStrip_ = rhs.isFinalStrip_;
		isFinalStrip_ = rhs.isFinalStrip_;
		headerSize_ = rhs.headerSize_;
		pool_ = rhs.pool_;
	}
	BufDim first_;
	BufDim last_;
	uint64_t writeSize_;
	bool isFirstStrip_;
	bool isFinalStrip_;
	uint64_t headerSize_;
	IBufferPool *pool_;
};
struct IOChunk : public RefCounted<IOChunk> {
	IOChunk(uint64_t offset,uint64_t len, IBufferPool *pool) :
		offset_(offset),
		len_(len),
		buf_(nullptr),
		acquireCount_(0),
		acquireTarget_(1)
	{
		if (pool){
			alloc(pool);
			share();
		}
	}
private:
	~IOChunk() {
		RefManager<IOBuf>::unref(buf_);
	}
public:
	IOChunk* share(void){
		acquireTarget_++;
		assert(buf_->data_);

		return ref();
	}
	bool isShared(void){
		return acquireTarget_ > 1;
	}
	void setHeader(uint8_t *headerData, uint64_t headerSize){
		memcpy(buf_->data_ , headerData, headerSize);
		buf_->skip_ = headerSize;
	}
	bool acquire(void){
		 return (++acquireCount_ == acquireTarget_);
	}
	IOBuf* transferBuf(void){
		auto b = buf_;
		buf_ = nullptr;

		return b;
	}
	void alloc(IBufferPool* pool){
		assert(!buf_ || buf_->data_);
		assert(pool);
		if (buf_ && buf_->data_)
			return;
		buf_ = pool->get(len_);
		buf_->offset_ = offset_;
	}
	uint64_t offset_;
	uint64_t len_;
private:
	IOBuf *buf_;
	std::atomic<uint32_t> acquireCount_;
	uint32_t acquireTarget_;
};


/**
 * A strip chunk wraps an IOChunk (which may be shared with the
 * strip's neighbour), and it also stores a write offset and write length
 * for its assigned portion of a shared IOChunk.
 * If there is no sharing, then  write offset is zero,
 * and write length equals WRTSIZE.
 */
struct StripChunk : public RefCounted<StripChunk> {
	StripChunk(IOChunk *ioChunk,
				uint64_t writeableOffset,
				uint64_t writeableLen) :
		ioChunk_(ioChunk),
		writeableOffset_(writeableOffset),
		writeableLen_(writeableLen)
	{
		assert(writeableOffset < len());
		assert(writeableLen <= len());
	}
private:
	~StripChunk(){
		RefManager<IOChunk>::unref(ioChunk_);
	}
public:
	void alloc(IBufferPool* pool){
		ioChunk_->alloc(pool);
	}
	uint64_t offset(void){
		return ioChunk_->offset_;
	}
	uint64_t len(void){
		return ioChunk_->len_;
	}
	bool acquire(void){
		return ioChunk_->acquire();
	}
	bool isShared(void){
		return ioChunk_->isShared();
	}
	void setHeader(uint8_t *headerData, uint64_t headerSize){
		ioChunk_->setHeader(headerData, headerSize);
		writeableOffset_ = headerSize;
	}
	// relative to beginning of IOBuf data buffer
	uint64_t writeableOffset_;
	uint64_t writeableLen_;
	IOChunk *ioChunk_;
};


/**
 * Container for an array of buffers and an array of chunks
 *
 */
struct StripChunkArray{
	StripChunkArray(StripChunk** chunks, IOBuf **buffers,uint32_t numBuffers, IBufferPool *pool)
		: ioBufs_(buffers),
		  stripChunks_(chunks),
		  numBuffers_(numBuffers),
		  pool_(pool)
	{
		for (uint32_t i = 0; i < numBuffers_; ++i)
			assert(ioBufs_[i]->data_);
	}
	~StripChunkArray(void){
		delete[] ioBufs_;
		delete[] stripChunks_;
	}
	IOBuf ** ioBufs_;
	StripChunk ** stripChunks_;
	uint32_t numBuffers_;
	IBufferPool *pool_;
};

/**
 * A strip contains an array of StripChunks
 *
 */
struct Strip  {
	Strip(uint64_t offset,uint64_t len,Strip* neighbour) :
		logicalOffset_(offset),
		logicalLen_(len),
		stripChunks_(nullptr),
		numChunks_(0),
		leftNeighbour_(neighbour)
	{}
	~Strip(void){
		if (stripChunks_){
			for (uint32_t i = 0; i < numChunks_; ++i)
				RefManager<StripChunk>::unref(stripChunks_[i]);
			delete[] stripChunks_;
		}
	}
	void generateChunks(ChunkInfo chunkInfo, IBufferPool *pool){
		chunkInfo_ = chunkInfo;
		numChunks_ = chunkInfo_.numChunks();
		assert(numChunks_);
		stripChunks_ = new StripChunk*[numChunks_];
		uint64_t writeableTotal = 0;
		bool singleStripSingleChunk = numChunks_ == 1 && chunkInfo.isFirstStrip_;
		if (numChunks_ == 1){
			auto ioChunk = 	new IOChunk(0,chunkInfo.first_.x1_ ,nullptr);
			stripChunks_[0] =
					new StripChunk(ioChunk,
								chunkInfo.isFirstStrip_ ?
									chunkInfo_.headerSize_ :
										chunkInfo.first_.x0_,
								chunkInfo.isFirstStrip_ ?
									chunkInfo.first_.x1_ - chunkInfo_.headerSize_ :
										chunkInfo.first_.len());
			writeableTotal = stripChunks_[0]->writeableLen_;
		}
		for (uint32_t i = 0; i < numChunks_ && numChunks_>1; ++i ){
			uint64_t off = (chunkInfo.first_.x1_ - chunkInfo_.writeSize_) +
								i * chunkInfo_.writeSize_;
			bool lastChunkOfAll  = chunkInfo.isFinalStrip_ && (i == numChunks_-1);
			uint64_t len =
					lastChunkOfAll ? (chunkInfo_.last_.len()) : chunkInfo_.writeSize_;
			uint64_t writeableOffset = 0;
			uint64_t writeableLen = len;
			bool sharedLastChunk = false;
			bool firstSeam       = (i == 0) && chunkInfo.hasFirstSeam();
			bool lastSeam        =
					(i == numChunks_-1) && !lastChunkOfAll && chunkInfo.hasLastSeam();
			if (firstSeam){
				assert(leftNeighbour_);
				off = leftNeighbour_->finalChunk()->offset();
				assert(chunkInfo_.first_.x0_  > off);
				writeableOffset = chunkInfo_.first_.x0_ - off;
				writeableLen    = chunkInfo_.first_.len();
				assert(writeableLen && writeableLen < chunkInfo_.writeSize_);
				assert(writeableOffset && writeableOffset < chunkInfo_.writeSize_);
			} else if (lastSeam){
				off   = chunkInfo_.last_.x0_;
				writeableLen = chunkInfo_.last_.len();
				assert(writeableLen && writeableLen < chunkInfo_.writeSize_);
				if (lastChunkOfAll)
					len = writeableLen;
				else
					sharedLastChunk = true;
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
			if (firstSeam)
				ioChunk = leftNeighbour_->finalChunk()->ioChunk_;
			else
				ioChunk = new IOChunk(off,len,(sharedLastChunk ? pool : nullptr));
			assert(!firstSeam || ioChunk->isShared());
			stripChunks_[i] = new StripChunk(ioChunk,
												writeableOffset,
												writeableLen);
		}

		// validation
		uint64_t writeableEnd = 0;
		if (numChunks_ > 1) {
			writeableEnd =	stripChunks_[numChunks_-1]->offset() +
					(singleStripSingleChunk ? chunkInfo_.headerSize_ : 0)
									+ stripChunks_[numChunks_-1]->writeableLen_;
		}
		else {
			writeableEnd = stripChunks_[0]->offset() +
							stripChunks_[0]->writeableOffset_ +
								stripChunks_[0]->writeableLen_;
		}

		assert(writeableEnd == chunkInfo_.last_.x1_);
		assert(!chunkInfo.isFirstStrip_ || stripChunks_[0]->offset() == 0);

		uint64_t writeableBegin =
				stripChunks_[0]->offset() + stripChunks_[0]->writeableOffset_;

		assert(writeableBegin ==
				chunkInfo_.first_.x0_ +
					(chunkInfo_.isFirstStrip_ ? chunkInfo_.headerSize_ : 0));
		assert(writeableEnd - writeableBegin == logicalLen_);
		assert(logicalLen_ == writeableTotal);
	}
	StripChunkArray* getStripChunkArray(IBufferPool *pool, uint8_t *header, uint64_t headerLen){
		auto buffers = new IOBuf*[numChunks_];
		auto chunks  = new StripChunk*[numChunks_];
		for (uint32_t i = 0; i < numChunks_; ++i){
			auto stripChunk = stripChunks_[i];
			auto ioChunk 	= stripChunk->ioChunk_;
			stripChunk->alloc(pool);
			if (header && i == 0)
				stripChunk->setHeader(header, headerLen);
			chunks[i] 	= stripChunk;
			buffers[i] 	= ioChunk->transferBuf();
		}
		for (uint32_t i = 0; i < numChunks_; ++i){
			assert(buffers[i]->data_);
			assert(buffers[i]->len_);
		}

		return new StripChunkArray(chunks,buffers,numChunks_,pool);
	}
	StripChunk* finalChunk(void){
		return stripChunks_[numChunks_-1];
	}
	StripChunk* firstChunk(void){
		return stripChunks_[0];
	}
	uint64_t logicalOffset_;
	uint64_t logicalLen_;
	StripChunk** stripChunks_;
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
		finalStripHeight_((nominalStripHeight && (height % nominalStripHeight != 0)) ?
							height - ((height / nominalStripHeight) * nominalStripHeight) :
								nominalStripHeight),
		headerSize_(headerSize),
		writeSize_(writeSize),
		finalStrip_(numStrips_-1),
		strips_(new Strip*[numStrips_])
	{
		for (uint32_t i = 0; i < numStrips_; ++i){
			auto neighbour = (i > 0) ? strips_[i-1] : nullptr;
			strips_[i] =
					new Strip(i * nominalStripHeight_ * stripPackedByteWidth_,
								stripHeight(i) * stripPackedByteWidth_,
								neighbour);
			if (pool)
				strips_[i]->generateChunks(getChunkInfo(i), pool);
		}

		// validation
		if (pool && numStrips_ > 1){
			for (uint32_t i = 0; i < numStrips_-1; ++i){
				auto curr = strips_[i];
				auto next = strips_[i+1];
				auto currFinal = curr->finalChunk();
				auto nextFirst =  next->firstChunk();
				if (currFinal->isShared()){
					//assert(currFinal->writeableLen_ +
					//		nextFirst->writeableLen_ == WRTSIZE);
					//assert(currFinal->writeableLen_ ==
					//			nextFirst->writeableOffset_);
				}
			}
		}
	}
	~ImageStripper(void){
		if (strips_){
			for (uint32_t i = 0; i < numStrips_; ++i)
				delete strips_[i];
			delete[] strips_;
		}
	}
	Strip* getStrip(uint32_t strip) const{
		return strips_[strip];
	}
	uint32_t numStrips(void) const{
		return numStrips_;
	}
	ChunkInfo getChunkInfo(uint32_t strip){
		return ChunkInfo(strip == 0,
						strip == finalStrip_,
						strips_[strip]->logicalOffset_,
						strips_[strip]->logicalLen_,
						strip == 0 ? 0 : strips_[strip-1]->logicalOffset_,
						strip == 0 ? 0 : strips_[strip-1]->logicalLen_,
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
	uint64_t stripPackedByteWidth_;
	uint32_t finalStripHeight_;
	uint32_t numStrips_;
	uint64_t headerSize_;
	uint64_t writeSize_;
	uint32_t finalStrip_;
	Strip **strips_;
};
