#pragma once

#include <cstdint>

#include "iobench_config.h"
#include "IFileIO.h"
#include "FileUringIO.h"
#include "BufferPool.h"

const int32_t invalid_fd = -1;

struct Serializer : public ISerializer
{
	Serializer(bool flushOnClose);
	~Serializer(void);
	void setMaxPooledRequests(uint32_t maxRequests);
	void registerApplicationClient(void);
	void registerClientCallback(io_callback reclaim_callback, void* user_data);
	bool attach(Serializer *parent);
	bool open(std::string name, std::string mode, bool asynch);
	bool close(void);
	uint64_t write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) override;
	uint64_t write(uint8_t* buf, uint64_t size);
	uint64_t seek(int64_t off, int32_t whence);
	IOBuf* getPoolBuffer(uint64_t len);
	IBufferPool* getPool(void);
	void enableSimulateWrite(void);
  private:
#ifdef IOBENCH_HAVE_URING
	FileUringIO uring;
#endif
	int getMode(std::string mode);
	int fd_;
	uint32_t numSimulatedWrites_;
	// used to detect when library-orchestrated encode is complete
	uint32_t maxSimulatedWrites_;
	uint64_t off_;
	io_callback reclaim_callback_;
	void* reclaim_user_data_;
	std::string filename_;
	std::string mode_;
	IBufferPool *pool_;
	bool ownsFileDescriptor_;
	bool simulateWrite_;
	bool flushOnClose_;
};
