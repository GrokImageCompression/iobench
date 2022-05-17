#pragma once

#include <cstdint>
#include <atomic>

class IRefCounted {
friend class RefReaper;
protected:
	virtual ~IRefCounted() = default;
private:
	virtual uint32_t unref(void)= 0;
};

template <typename T> class RefCounted : public IRefCounted {
public:
	RefCounted(void) : refCount_(1)
	{}
	T* ref(void){
	   ++refCount_;

	   return (T*)this;
	}
protected:
	virtual ~RefCounted() = default;
private:
	uint32_t unref(void) override {
		assert(refCount_ > 0);
		return --refCount_;
	}
	std::atomic<uint32_t> refCount_;
};

class RefReaper{
public:
	static void unref(IRefCounted *refCounted){
		if (!refCounted)
			return;
		if (refCounted->unref() == 0)
			delete refCounted;
	}
};

