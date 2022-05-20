#pragma once

#include "config.h"
#ifdef IOBENCH_HAVE_URING

#include <liburing.h>
#include <liburing/io_uring.h>

#include "IFileIO.h"

namespace iobench {

class FileIOUring : public IFileIO
{
  public:
	FileIOUring();
	virtual ~FileIOUring() override;
	bool close(void) override;
	uint64_t write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) override;

	// uring-specific
	void registerReclaimCallback(io_callback reclaim_callback, void* user_data);
	bool attach(std::string fileName, std::string mode, int fd, int shared_ring_fd);
	bool attach(const FileIOUring *parent);
	bool active(void) const;

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
	IOScheduleData* retrieveCompletion(bool peek, bool& success);

	const uint32_t QD = 1024;
	io_callback reclaim_callback_;
	void* reclaim_user_data_;
};

}

#endif
