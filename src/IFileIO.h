#pragma once

#include <string>
#include "library.h"
#include <cstdlib>

#define K 1024
#define ALIGNMENT (4*K)
#define WRTSIZE (16*K)

struct SerializeBuf : public serialize_buf
{
  public:
	SerializeBuf() : SerializeBuf(nullptr, 0, 0, 0, false) {}
	SerializeBuf(uint8_t* data, uint64_t offset, uint64_t dataLen, uint64_t allocLen,
					bool pooled) : header_(nullptr), headerSize_(0)
	{
		this->data = data;
		this->offset = offset;
		this->dataLen = dataLen;
		this->allocLen = allocLen;
		this->pooled = pooled;
	}
	explicit SerializeBuf(const serialize_buf rhs) : header_(nullptr), headerSize_(0)
	{
		data = rhs.data;
		offset = rhs.offset;
		dataLen = rhs.dataLen;
		allocLen = rhs.allocLen;
		pooled = rhs.pooled;
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
	}
	uint8_t *header_;
	uint32_t headerSize_;
};

class IFileIO
{
  public:
	virtual ~IFileIO() = default;
	virtual bool close(void) = 0;
	virtual uint64_t write(SerializeBuf buffer) = 0;
};
