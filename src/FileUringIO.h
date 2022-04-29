#pragma once

#include "IFileIO.h"
#include <liburing.h>
#include <liburing/io_uring.h>
#include <mutex>

struct io_data
{
	io_data() : iov{0, 0} {}
	SerializeBuf buf;
	iovec iov;
};

class FileUringIO : public IFileIO
{
  public:
	FileUringIO();
	virtual ~FileUringIO() override;
	void registerClientCallback(serialize_callback reclaim_callback, void* user_data);
	bool attach(std::string fileName, std::string mode, int fd);
	bool attach(FileUringIO *parent);
	bool close(void) override;
	uint64_t write(SerializeBuf buffer) override;
	io_data* retrieveCompletion(bool peek, bool& success);

  private:
	io_uring ring;
	int fd_;
	bool ownsDescriptor;
	std::string fileName_;
	std::string mode_;
	size_t requestsSubmitted;
	size_t requestsCompleted;
	void enqueue(io_uring* ring, io_data* data, bool readop, int fd);
	bool initQueue(void);

	const uint32_t QD = 1024;
	serialize_callback reclaim_callback_;
	void* reclaim_user_data_;
};
