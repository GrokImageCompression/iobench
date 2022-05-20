#pragma once

#include <string>
#include <cstdlib>
#include <cstring>
#include <cassert>
#ifdef _WIN32
#include <malloc.h>
#endif

#include "library.h"
#include "RefCounted.h"

#define K 1024
#define ALIGNMENT (512)
#define WRTSIZE (64*K)


struct IOBuf : public io_buf, public RefCounted
{
  public:
	IOBuf() {
		index_ = 0;
		skip_ = 0;
		offset_ = 0;
		data_ = 0;
		len_ = 0;
		allocLen_ = 0;
	}
  private:
  	~IOBuf() {
  		dealloc();
  	}
  public:
	static bool isAlignedToWriteSize(uint64_t off){
		return (off & (WRTSIZE-1)) == 0;
	}
	bool aligned(void){
		return alignedOffset() && alignedLength();
	}
	bool alignedOffset(void){
		return isAlignedToWriteSize(offset_);
	}
	bool alignedLength(void){
		return isAlignedToWriteSize(len_);
	}
	bool alloc(uint64_t len)
	{
		if (len < allocLen_)
			return true;

		if (data_)
			dealloc();
#ifdef _WIN32
		data_ = (uint8_t*)_aligned_malloc(len,ALIGNMENT);
#else
		data_ = (uint8_t*)std::aligned_alloc(ALIGNMENT,len);
#endif
		if(data_)
		{
			len_ = len;
			allocLen_ = len;
		}
		assert(data_);

		return data_ != nullptr;
	}
	void updateLen(uint64_t len){
		if (data_ && len <= allocLen_)
			len_ = len;
	}
	void dealloc()
	{
		free(data_);
		data_ = nullptr;
		len_ = 0;
		allocLen_ = 0;
	}
};

// mirror of iovec struct
struct io {
  void *iov_base;
  size_t iov_len;
};

struct IOScheduleData
{
	IOScheduleData(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) :
		offset_(offset) , numBuffers_(numBuffers),buffers_(nullptr),
		iov_(new io[numBuffers_]), totalBytes_(0)
	{
		assert(numBuffers);
		buffers_ = new IOBuf*[numBuffers];
		for (uint32_t i = 0; i < numBuffers_; ++i){
			buffers_[i] = buffers[i];
			auto b = buffers_[i];
			auto v = iov_ + i;
			v->iov_base = b->data_;
			v->iov_len  = b->len_;
			totalBytes_   += b->len_;
		}
	}
	~IOScheduleData(){
		delete[] buffers_;
		delete[] iov_;
	}
	uint64_t offset_;
	uint32_t numBuffers_;
	IOBuf **buffers_;
	io *iov_;
	uint64_t totalBytes_;
};

class ISerializer{
public:
	virtual ~ISerializer() = default;
	virtual uint64_t write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) = 0;
};

class IFileIO
{
  public:
	virtual ~IFileIO() = default;
	virtual bool close(void) = 0;
	virtual uint64_t write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) = 0;
};
