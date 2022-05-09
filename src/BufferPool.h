#pragma once

#include <cstdint>
#include <map>
#include <thread>
#include <mutex>

#include "IFileIO.h"
#include "IBufferPool.h"

class Locker
{
  public:
	Locker(std::mutex &mut) : lock_(mut)
	{}
  private:
	std::lock_guard<std::mutex> lock_;
};

class FakeLocker
{
  public:
	FakeLocker(std::mutex &mut){
		(void)mut;
	}
};


template <typename L> class BufferPool : public IBufferPool
{
  public:
	BufferPool() = default;
	virtual ~BufferPool(){
		for(auto& p : pool)
			p.second.dealloc();
	}
	SerializeBuf get(uint64_t len) override{
		L lck(mut_);
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
		SerializeBuf rc;
		rc.alloc(len);

		return rc;
	}
	void put(SerializeBuf b) override{
		L lck(mut_);
		assert(b.data);
		assert(pool.find(b.data) == pool.end());
		pool[b.data] = b;
	}
  private:
	std::map<uint8_t*, SerializeBuf> pool;
	std::mutex mut_;
};
