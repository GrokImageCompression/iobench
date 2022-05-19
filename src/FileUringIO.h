#pragma once

#include "iobench_config.h"
#ifdef IOBENCH_HAVE_URING

#include "IFileIO.h"
#include <liburing.h>
#include <liburing/io_uring.h>
#include <mutex>

class FileUringIO : public IFileIO
{
  public:
	FileUringIO();
	virtual ~FileUringIO() override;
	void registerClientCallback(io_callback reclaim_callback, void* user_data);
	bool attach(std::string fileName, std::string mode, int fd, int shared_ring_fd);
	bool attach(FileUringIO *parent);
	bool close(void) override;
	uint64_t write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) override;
	IOScheduleData* retrieveCompletion(bool peek, bool& success);
	bool active(void);

  private:
	io_uring ring;
	int fd_;
	bool ownsDescriptor;
	std::string fileName_;
	std::string mode_;
	size_t requestsSubmitted;
	size_t requestsCompleted;
	void enqueue(io_uring* ring, IOScheduleData* data, bool readop, int fd);
	bool initQueue(int shared_ring_fd);

	const uint32_t QD = 1024;
	io_callback reclaim_callback_;
	void* reclaim_user_data_;
};

#endif
