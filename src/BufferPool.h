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
			p.second->dealloc();
	}
	IOBuf* get(uint64_t len) override{
		for(auto iter = pool.begin(); iter != pool.end(); ++iter)
		{
			if(iter->second->allocLen >= len)
			{
				auto b = iter->second;
				assert(b->data);
				pool.erase(iter);
				assert(b->data);
				return b;
			}
		}
		auto buf = new IOBuf();
		buf->alloc(len);
		assert(buf->data);
		buf->pooled = true;
		return buf;
	}
	void put(IOBuf *b) override{
		assert(b->data);
		if (pool.find(b->data) != pool.end())
			return;
		pool[b->data] = b;
	}
  private:
	std::map<uint8_t*, IOBuf*> pool;
};
