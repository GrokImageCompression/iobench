#pragma once

#include "IFileIO.h"

class IBufferPool
{
  public:
	virtual ~IBufferPool() = default;
	virtual SerializeBuf get(uint64_t len) = 0;
	virtual void put(SerializeBuf b) = 0;
};
