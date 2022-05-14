#pragma once

#include <cstdint>
#include <atomic>

template <typename T> class RefCounted {
template <typename U> friend class RefManager;
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
	uint32_t unref(void) {
		assert(refCount_ > 0);
		return --refCount_;
	}
	std::atomic<uint32_t> refCount_;
};

template <typename T> class RefManager{
public:
	static void unref(RefCounted<T> *refCounted){
		if (!refCounted)
			return;
		if (refCounted->unref() == 0)
			delete refCounted;
	}
};

