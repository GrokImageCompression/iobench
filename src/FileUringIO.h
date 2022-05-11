#pragma once

#include "IFileIO.h"
#include <liburing.h>
#include <liburing/io_uring.h>
#include <mutex>

struct io_data
{
	io_data() : io_data(0,nullptr,0)
	{}
	io_data(uint64_t offset, SerializeBuf *buffers, uint32_t numBuffers) :
		offset_(offset) , numBuffers_(numBuffers),buffers_(nullptr),
		iov_(numBuffers ? new iovec[numBuffers] : nullptr)
	{
		if (buffers)
			buffers_ = new SerializeBuf[numBuffers];
		for (uint32_t i = 0; i < numBuffers_; ++i){
			buffers_[i] = buffers[i];
			auto b = buffers_ + i;
			auto v = iov_ + i;
			iov_->iov_base = b->data;
			iov_->iov_len = b->dataLen;
		}
	}
	~io_data(){
		delete[] buffers_;
		delete[] iov_;
	}
	uint64_t offset_;
	uint32_t numBuffers_;
	SerializeBuf *buffers_;
	iovec *iov_;
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
