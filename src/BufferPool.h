#pragma once

#include <cstdint>
#include <map>
#include <thread>
#include <mutex>

#include "IFileIO.h"
#include "IBufferPool.h"


class BufferPool : public IBufferPool
{
  public:
	BufferPool() = default;
	virtual ~BufferPool(){
		for(auto& p : pool)
			p.second.dealloc();
	}
	IOBuf get(uint64_t len) override{
		for(auto iter = pool.begin(); iter != pool.end(); ++iter)
		{
			if(iter->second.allocLen >= len)
			{
				auto b = iter->second;
				b.dataLen = len;
				pool.erase(iter);
				return b;
			}
		}
		IOBuf rc;
		rc.alloc(len);

		return rc;
	}
	void put(IOBuf b) override{
		assert(b.data);
		assert(pool.find(b.data) == pool.end());
		pool[b.data] = b;
	}
  private:
	std::map<uint8_t*, IOBuf> pool;
};
