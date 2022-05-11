#pragma once

#include "IFileIO.h"
#include <liburing.h>
#include <liburing/io_uring.h>
#include <mutex>

struct io_data
{
	io_data() : offset_(0) , numBuffers_(0),buffers_(nullptr)
	{}
	~io_data(){
		delete buffers_;
	}
	uint64_t offset_;
	uint32_t numBuffers_;
	SerializeBuf *buffers_;
	iovec iov;
};

class FileUringIO : public IFileIO
{
  public:
	FileUringIO();
	virtual ~FileUringIO() override;
	void registerClientCallback(serialize_callback reclaim_callback, void* user_data);
	bool attach(std::string fileName, std::string mode, int fd, int shared_ring_fd);
	bool attach(FileUringIO *parent);
	bool close(void) override;
	uint64_t write(uint64_t offset, SerializeBuf *buffers, uint32_t numBuffers) override;
	io_data* retrieveCompletion(bool peek, bool& success);
	bool active(void);

  private:
	io_uring ring;
	int fd_;
	bool ownsDescriptor;
	std::string fileName_;
	std::string mode_;
	size_t requestsSubmitted;
	size_t requestsCompleted;
	void enqueue(io_uring* ring, io_data* data, bool readop, int fd);
	bool initQueue(int shared_ring_fd);

	const uint32_t QD = 1024;
	serialize_callback reclaim_callback_;
	void* reclaim_user_data_;
};
