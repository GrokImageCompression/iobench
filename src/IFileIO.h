#pragma once

#include <string>
#include "library.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>

#define K 1024
#define ALIGNMENT (512)
#define WRTSIZE (4*K)


struct IOBuf : public io_buf
{
  public:
	IOBuf() {
		this->index = 0;
		this->skip = 0;
		this->offset = 0;
		this->data = 0;
		this->dataLen = 0;
		this->allocLen = 0;
		this->pooled = false;

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
		}
		assert(data);
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

struct IOScheduleData
{
	IOScheduleData(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) :
		offset_(offset) , numBuffers_(numBuffers),buffers_(nullptr),
		iov_(new iovec[numBuffers_]), totalBytes_(0)
	{
		assert(numBuffers);
		buffers_ = new IOBuf*[numBuffers];
		for (uint32_t i = 0; i < numBuffers_; ++i){
			buffers_[i] = buffers[i];
			auto b = buffers_[i];
			auto v = iov_ + i;
			v->iov_base = b->data;
			v->iov_len  = b->dataLen;
			totalBytes_   += b->dataLen;
		}
	}
	~IOScheduleData(){
		delete[] buffers_;
		delete[] iov_;
	}
	uint64_t offset_;
	uint32_t numBuffers_;
	IOBuf **buffers_;
	iovec *iov_;
	uint64_t totalBytes_;
};

class ISerializeBufWriter{
public:
	virtual ~ISerializeBufWriter() = default;
	virtual uint64_t write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) = 0;
};

class IFileIO
{
  public:
	virtual ~IFileIO() = default;
	virtual bool close(void) = 0;
	virtual uint64_t write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) = 0;
};
