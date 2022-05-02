#pragma once

#include "IFileIO.h"
#include "FileUringIO.h"
#include "BufferPool.h"

#include <cstdint>

const int32_t invalid_fd = -1;

struct Serializer
{
	Serializer(void);
	~Serializer(void);
	void setMaxPooledRequests(uint32_t maxRequests);
	void registerApplicationClient(void);
	void registerClientCallback(serialize_callback reclaim_callback, void* user_data);
	void reclaimBuffer(serialize_buf buffer);
	bool attach(Serializer *parent);
	bool open(std::string name, std::string mode, bool asynch);
	bool close(void);
	size_t writeAsynch(SerializeBuf serializeBuf);
	size_t write(uint8_t* buf, uint64_t size);
	uint64_t seek(int64_t off, int32_t whence);
	void initPooledRequest(void);
	bool allPooledRequestsComplete(void);
	SerializeBuf getPoolBuffer(uint64_t len);
	void putPoolBuffer(SerializeBuf buf);
	bool isAsynch(void);
  private:
	FileUringIO uring;
	bool pooled_;
	int getMode(std::string mode);
	int fd_;
	uint32_t numPooledRequests_;
	// used to detect when library-orchestrated encode is complete
	uint32_t maxPooledRequests_;
	bool asynch_;
	uint64_t off_;
	serialize_callback reclaim_callback_;
	void* reclaim_user_data_;
	std::string filename_;
	std::string mode_;
	BufferPool pool_;
	bool ownsFileDescriptor_;
};
