#pragma once

#include <string>
#include "library.h"

struct SerializeBuf : public serialize_buf
{
  public:
	SerializeBuf() : SerializeBuf(nullptr, 0, 0, 0, false) {}
	SerializeBuf(uint8_t* data, uint64_t offset, uint64_t dataLen, uint64_t allocLen,
					bool pooled)
	{
		this->data = data;
		this->offset = offset;
		this->dataLen = dataLen;
		this->allocLen = allocLen;
		this->pooled = pooled;
	}
	explicit SerializeBuf(const serialize_buf rhs)
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
		data = new uint8_t[len];
		if(data)
		{
			dataLen = len;
			allocLen = len;
		}

		return data != nullptr;
		;
	}
	void dealloc()
	{
		delete[] data;
		data = nullptr;
	}
};

class IFileIO
{
  public:
	virtual ~IFileIO() = default;
	virtual bool close(void) = 0;
	virtual uint64_t write(SerializeBuf buffer) = 0;
};
