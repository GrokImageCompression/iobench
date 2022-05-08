#pragma once

#include <string>
#include "library.h"
#include <cstdlib>

#define K 1024
#define ALIGNMENT (512)
#define WRTSIZE (16*K)

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
	explicit SerializeBuf(const serialize_buf rhs)
	{
		skip = rhs.skip;
		offset = rhs.offset;
		data = rhs.data;
		dataLen = rhs.dataLen;
		allocLen = rhs.allocLen;
		pooled = rhs.pooled;
	}
	static bool isAligned(uint64_t off){
		return (off & (ALIGNMENT-1)) == 0;
	}
	bool aligned(void){
		return alignedOffset() && alignedLength();
	}
	bool alignedOffset(void){
		return isAligned(offset);
	}
	bool alignedLength(void){
		return isAligned(dataLen);
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

class ISerializeBufWriter{
public:
	virtual ~ISerializeBufWriter() = default;
	virtual size_t write(SerializeBuf buf) = 0;
};

class IFileIO
{
  public:
	virtual ~IFileIO() = default;
	virtual bool close(void) = 0;
	virtual uint64_t write(SerializeBuf buffer) = 0;
};
