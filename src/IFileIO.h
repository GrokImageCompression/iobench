#pragma once

#include <string>
#include "library.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>

#define K 1024
#define ALIGNMENT (512)
#define WRTSIZE (64*K)


struct SerializeBuf : public serialize_buf
{
  public:
	SerializeBuf() : SerializeBuf(0,nullptr,0, 0, 0, 0, false) {}
	SerializeBuf(uint8_t* data,uint64_t allocLen) :
		SerializeBuf(0,data,0,0,0,allocLen,false)
	{}
	SerializeBuf(uint32_t index, uint8_t* data, uint64_t skip,
				uint64_t offset, uint64_t dataLen, uint64_t allocLen,
					bool pooled)
	{
		this->index = index;
		this->skip = skip;
		this->offset = offset;
		this->data = data;
		this->dataLen = dataLen;
		this->allocLen = allocLen;
		this->pooled = pooled;
	}
	SerializeBuf(const SerializeBuf &rhs) : SerializeBuf(&rhs)
	{}
	SerializeBuf(const SerializeBuf *rhs)
	{
		skip = rhs->skip;
		offset = rhs->offset;
		data = rhs->data;
		dataLen = rhs->dataLen;
		allocLen = rhs->allocLen;
		pooled = rhs->pooled;
	}
	explicit SerializeBuf(const serialize_buf &rhs) : SerializeBuf(&rhs)
	{}
	explicit SerializeBuf(const serialize_buf *rhs)
	{
		skip = rhs->skip;
		offset = rhs->offset;
		data = rhs->data;
		dataLen = rhs->dataLen;
		allocLen = rhs->allocLen;
		pooled = rhs->pooled;
	}
	static bool isAlignedToWriteSize(uint64_t off){
		return (off & (WRTSIZE-1)) == 0;
	}
	bool aligned(void){
		return alignedOffset() && alignedLength();
	}
	bool alignedOffset(void){
		return isAlignedToWriteSize(offset);
	}
	bool alignedLength(void){
		return isAlignedToWriteSize(dataLen);
	}
	bool alloc(uint64_t len)
	{
		dealloc();
		data = (uint8_t*)aligned_alloc(ALIGNMENT,len);
		if(data)
		{
			dataLen = len;
			allocLen = len;
			memset(data,0,allocLen);
		}

		return data != nullptr;
	}
	void dealloc()
	{
		free(data);
		data = nullptr;
		dataLen = 0;
		allocLen = 0;
	}
};

struct io_data
{
	io_data(uint64_t offset, SerializeBuf **buffers, uint32_t numBuffers) :
		offset_(offset) , numBuffers_(numBuffers),buffers_(nullptr),
		iov_(numBuffers ? new iovec[numBuffers] : nullptr), totalBytes_(0)
	{
		if (buffers)
			buffers_ = new SerializeBuf*[numBuffers];
		for (uint32_t i = 0; i < numBuffers_; ++i){
			buffers_[i] = new SerializeBuf(buffers[i]);
			auto b = buffers_[i];
			auto v = iov_ + i;
			iov_->iov_base = b->data;
			iov_->iov_len = b->dataLen;
			totalBytes_ += b->dataLen;
		}
	}
	~io_data(){
		for (uint32_t i = 0; i < numBuffers_; ++i)
			delete buffers_[i];
		delete[] buffers_;
		delete[] iov_;
	}
	uint64_t offset_;
	uint32_t numBuffers_;
	SerializeBuf **buffers_;
	iovec *iov_;
	uint64_t totalBytes_;
};

class ISerializeBufWriter{
public:
	virtual ~ISerializeBufWriter() = default;
	virtual uint64_t write(uint64_t offset, SerializeBuf **buffers, uint32_t numBuffers) = 0;
};

class IFileIO
{
  public:
	virtual ~IFileIO() = default;
	virtual bool close(void) = 0;
	virtual uint64_t write(uint64_t offset, SerializeBuf **buffers, uint32_t numBuffers) = 0;
};
