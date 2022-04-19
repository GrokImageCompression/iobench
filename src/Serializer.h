#pragma once

#include "IFileIO.h"
#include "FileUringIO.h"

#include <cstdint>

struct Serializer
{
	Serializer(void);
	void setMaxPooledRequests(uint32_t maxRequests);
	void serializeRegisterClientCallback(serialize_callback reclaim_callback, void* user_data);
	serialize_callback getSerializerReclaimCallback(void);
	void* getSerializerReclaimUserData(void);
	int getFd(void);
	bool open(std::string name, std::string mode, bool asynch);
	bool close(void);
	size_t write(uint8_t* buf, size_t size);
	uint64_t seek(int64_t off, int32_t whence);
	uint32_t getNumPooledRequests(void);
	uint64_t getOffset(void);
	void initPooledRequest(void);
	bool allPooledRequestsComplete(void);

  private:
	FileUringIO uring;
	SerializeBuf scheduled_;
	int getMode(std::string mode);
	int fd_;
	uint32_t numPooledRequests_;
	// used to detect when library-orchestrated encode is complete
	uint32_t maxPooledRequests_;
	bool asynchActive_;
	uint64_t off_;
	serialize_callback reclaim_callback_;
	void* reclaim_user_data_;
};
