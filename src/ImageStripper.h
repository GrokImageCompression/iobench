#pragma once

#include <cstdint>
#include <atomic>
#include <thread>

#include "IBufferPool.h"
#include <mutex>

struct SerializeBufArray{
	SerializeBufArray(SerializeBuf **buffers, uint32_t numBuffers, IBufferPool *pool)
		: buffers_(buffers), numBuffers_(numBuffers), pool_(pool)
	{}
	~SerializeBufArray(void){
		for (uint32_t i = 0; i < numBuffers_; ++i){
			pool_->put(*buffers_[i]);
			delete buffers_[i];
		}
		delete[] buffers_;
	}
	SerializeBuf ** buffers_;
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

struct SerializeChunkInfo{
	SerializeChunkInfo(void) :    firstBegin_(0), firstEnd_(0),
					lastBegin_(0), lastEnd_(0),
					writeSize_(0), isFirstStrip_(false),
					isFinalStrip_(false), headerSize_(0),
					pool_(nullptr)
	{}
	uint64_t len(){
		return lastEnd_ - firstBegin_;
	}
	uint32_t numChunks(void){
		uint64_t nonSeamBegin = hasFirst() ? firstEnd_ : firstBegin_;
		uint64_t nonSeamEnd   = hasLast() ? lastBegin_ : lastEnd_;
		assert(nonSeamEnd > nonSeamBegin );
		assert(SerializeBuf::isAlignedToWriteSize(nonSeamBegin));
		assert(isFinalStrip_ || SerializeBuf::isAlignedToWriteSize(nonSeamEnd));
		uint64_t rc = (nonSeamEnd - nonSeamBegin + writeSize_ - 1) / writeSize_;
		if (hasFirst())
			rc++;
		if (hasLast())
			rc++;
		assert(rc > 1);

		return rc;
	}
	bool hasFirst(void){
		return !isFirstStrip_ && !SerializeBuf::isAlignedToWriteSize(firstBegin_);
	}
	bool hasLast(void){
		return !isFinalStrip_ && !SerializeBuf::isAlignedToWriteSize(lastEnd_);
	}
	SerializeChunkInfo(const SerializeChunkInfo &rhs){
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
struct SerializeChunkBuffer{
	SerializeChunkBuffer() : SerializeChunkBuffer(0,0,nullptr)
	{}
	SerializeChunkBuffer(uint64_t offset,uint64_t len, IBufferPool *pool) :
		refCount_(1),writeCount_(0), writeTarget_(1)
	{
		buf_.offset = offset;
		buf_.dataLen = len;
		if (pool)
			alloc(pool);
	}
	SerializeChunkBuffer* ref(void){
		++refCount_;
		assert(refCount_ <= 2);
		return this;
	}
	uint32_t unref(void){
		return --refCount_;
	}
	bool submit(ISerializeBufWriter *writer){
		assert(writer);
		if (++writeCount_ == writeTarget_){
			auto bArray = new SerializeBuf*[1];
			bArray[0] = &buf_;
			bool rc =  writer->write(buf_.offset,bArray,1) == buf_.dataLen;
			delete[] bArray;
			return rc;
		}
		return true;
	}
	bool acquire(void){
		 return (++writeCount_ == writeTarget_);
	}
	void alloc(IBufferPool* pool){
		assert(pool);
		assert(buf_.dataLen);
		assert(!buf_.data);
		if (buf_.data)
			return;
		// cache offset
		uint64_t offset = buf_.offset;
		// allocate
		buf_ = pool->get(buf_.dataLen);
		// restore offset
		buf_.offset = offset;

	}
	SerializeBuf buf_;
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
struct StripChunkBuffer {
	StripChunkBuffer(SerializeChunkBuffer *serializeChunkBuffer,
			uint64_t writeableOffset, uint64_t writeableLen,
			bool shared) :
		serializeChunkBuffer_(serializeChunkBuffer),
		writeableOffset_(writeableOffset),
		writeableLen_(writeableLen),
		shared_(shared)
	{
		assert(writeableOffset < len());
		assert(writeableLen <= len());
	}
	~StripChunkBuffer(){
		if (unref() == 0)
			delete serializeChunkBuffer_;
	}
	void alloc(IBufferPool* pool){
		if (!shared_)
			serializeChunkBuffer_->alloc(pool);
	}
	uint64_t offset(void){
		return serializeChunkBuffer_->buf_.offset;
	}
	uint64_t len(void){
		return serializeChunkBuffer_->buf_.dataLen;
	}
	SerializeChunkBuffer* ref(void){
		return serializeChunkBuffer_->ref();
	}
	uint32_t unref(void){
		return serializeChunkBuffer_->unref();
	}
	bool submit(ISerializeBufWriter *writer){
		return serializeChunkBuffer_->submit(writer);
	}
	bool acquire(void){
		return serializeChunkBuffer_->acquire();
	}
	uint8_t* data(void){
		return serializeChunkBuffer_->buf_.data;
	}
	void setHeader(uint8_t *headerData, uint64_t headerSize){
		memcpy(data() , headerData, headerSize);
		serializeChunkBuffer_->buf_.skip = headerSize;
		writeableOffset_ = headerSize;
	}
	void setPooled(void){
		serializeChunkBuffer_->buf_.pooled = true;
	}
	// write offset relative to beginning of data
	uint64_t writeableOffset_;
	// writeable length
	uint64_t writeableLen_;
	bool shared_;
	SerializeChunkBuffer *serializeChunkBuffer_;
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
	void generateChunks(SerializeChunkInfo chunkInfo, IBufferPool *pool){
		chunkInfo_ = chunkInfo;
		numChunks_ = chunkInfo_.numChunks();
		assert(numChunks_);
		chunks_ = new StripChunkBuffer*[numChunks_];
		uint64_t writeableTotal = 0;
		for (uint32_t i = 0; i < numChunks_; ++i ){
			uint64_t off =
					(chunkInfo.firstEnd_ - chunkInfo_.writeSize_) + i * chunkInfo_.writeSize_;
			bool lastChunkOfAll  = chunkInfo.isFinalStrip_ && (i == numChunks_-1);
			uint64_t len     = lastChunkOfAll ?
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
			assert(SerializeBuf::isAlignedToWriteSize(off));
			assert(lastChunkOfAll || SerializeBuf::isAlignedToWriteSize(len));
			SerializeChunkBuffer* serializeChunkBuffer;
			if (firstSeam){
				serializeChunkBuffer = leftNeighbour_->finalChunk()->ref();
				assert(serializeChunkBuffer->buf_.data);
			}
			else
				serializeChunkBuffer =
						new SerializeChunkBuffer(off,len,(shared ? pool : nullptr));

			chunks_[i] = new StripChunkBuffer(serializeChunkBuffer,
												writeableOffset,
												writeableLen,
												shared);
			assert(!shared || chunks_[i]->serializeChunkBuffer_->buf_.data);
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
	bool nextChunk(IBufferPool *pool, StripChunkBuffer **chunkBuffer){
		assert(pool);
		assert(chunkBuffer);
		uint32_t chunk = ++nextChunkIndex_;
		if (chunk >= numChunks_)
			return false;
		chunks_[chunk]->alloc(pool);
		*chunkBuffer = chunks_[chunk];

		return true;
	}
	SerializeBufArray* genBufferArray(IBufferPool *pool){
		auto first = chunks_[0];
		bool acquiredFirst = true;
		if (first->shared_){
			acquiredFirst = first->acquire();
		}
		auto ret = new SerializeBuf*[numChunks_ - (acquiredFirst ? 0 : 1)];
		uint32_t count = 0;
		for (uint32_t i = 0; i < numChunks_; ++i){
			if (!acquiredFirst)
				continue;
			auto ch = chunks_[i];
			ch->alloc(pool);
			assert(ch->serializeChunkBuffer_->buf_.data);
			auto b = new SerializeBuf(ch->serializeChunkBuffer_->buf_);
			ch->serializeChunkBuffer_->buf_.data = nullptr;
			assert(b->data);
			ret[count++] = b;
		}

		return new SerializeBufArray(ret,count,pool);
	}

	StripChunkBuffer* finalChunk(void){
		return chunks_[numChunks_-1];
	}

	// independant of header size
	// (temporary)
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
	ImageStripper() : ImageStripper(0,0,0,0,0,0,nullptr)
	{}
	~ImageStripper(void){
		if (stripBuffers_){
			for (uint32_t i = 0; i < numStrips_; ++i)
				delete stripBuffers_[i];
			delete[] stripBuffers_;
		}
	}
	ImageStripper(uint32_t width, uint32_t height, uint16_t numcomps,
					uint32_t nominalStripHeight,
					uint64_t headerSize, uint64_t writeSize,
					IBufferPool *pool) :
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
			if (pool)
				stripBuffers_[i]->generateChunks(getSerializeChunkInfo(i), pool);
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
		ret.headerSize_   = headerSize_;
		ret.writeSize_    = writeSize_;
		ret.lastBegin_    = lastBeginHeaderCorrected(strip);
		assert(ret.lastBegin_% writeSize_ == 0);
		ret.lastEnd_      = stripEndHeaderCorrected(strip);
		ret.firstBegin_   = stripOffsetHeaderCorrected(strip);
		ret.firstEnd_     = (strip == 0 ? 0 : lastBeginHeaderCorrected(strip-1)) + writeSize_;
		assert(ret.firstEnd_% writeSize_ == 0);
		assert((ret.lastBegin_ - ret.firstEnd_) % writeSize_ == 0);
		assert(ret.firstEnd_ >= ret.firstBegin_);
		assert(ret.lastEnd_ >= ret.lastBegin_);
		assert(ret.firstEnd_ <= ret.lastBegin_);
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
	uint64_t stripOffsetHeaderCorrected(uint32_t strip){
		// header bytes added to first strip shifts all other strips
		// by that number of bytes
		return strip == 0 ? 0 : headerSize_ + stripBuffers_[strip]->offset_;
	}
	uint64_t stripEndHeaderCorrected(uint32_t strip){
		uint64_t rc = stripOffsetHeaderCorrected(strip) + stripBuffers_[strip]->len_;
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
	StripBuffer **stripBuffers_;
};
