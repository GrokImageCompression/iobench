#pragma once

#include "IFileIO.h"
#include "FileUringIO.h"
#include "BufferPool.h"

#include <cstdint>

const int32_t invalid_fd = -1;

enum SerializeState {
	SERIALIZE_STATE_NONE,
	SERIALIZE_STATE_ASYNCH_WRITE,
	SERIALIZE_STATE_SYNCH
};

struct Serializer
{
	Serializer(void);
	~Serializer(void);
	void setMaxPooledRequests(uint32_t maxRequests);
	void registerApplicationClient(void);
	void registerClientCallback(serialize_callback reclaim_callback, void* user_data);
	void reclaimBuffer(serialize_buf buffer);
	bool attach(Serializer *parent);
	bool open(std::string name, std::string mode, SerializeState serializeState);
	bool close(void);
	size_t writeAsynch(uint8_t* buf, uint64_t offset, uint64_t size, uint32_t index);
	size_t write(uint8_t* buf, uint64_t size);
	uint64_t seek(int64_t off, int32_t whence);
	uint32_t getNumPooledRequests(void);
	void initPooledRequest(void);
	bool allPooledRequestsComplete(void);
	SerializeBuf getPoolBuffer(uint64_t len);
	void putPoolBuffer(SerializeBuf buf);
	SerializeState getState(void);
	void setHeader(uint8_t *header, uint32_t headerSize);
  private:
	FileUringIO uring;
	SerializeBuf scheduled_;
	int getMode(std::string mode);
	int fd_;
	uint32_t numPooledRequests_;
	// used to detect when library-orchestrated encode is complete
	uint32_t maxPooledRequests_;
	SerializeState state_;
	uint64_t off_;
	serialize_callback reclaim_callback_;
	void* reclaim_user_data_;
	std::string filename_;
	std::string mode_;
	BufferPool pool_;
	uint8_t *header_;
	uint32_t headerSize_;
};
