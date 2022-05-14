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
		for(std::pair<uint8_t*, IOBuf*> p : pool)
			RefManager<IOBuf>::unref(p.second);
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
		auto b = new IOBuf();
		b->alloc(len);
		assert(b->data);
		return b;
	}
	void put(IOBuf *b) override{
		assert(b->data);
		assert(pool.find(b->data) == pool.end());
		pool[b->data] = b;
	}
  private:
	std::map<uint8_t*, IOBuf*> pool;
};
