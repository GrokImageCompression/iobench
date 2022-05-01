#include "FileUringIO.h"
#include <strings.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <chrono>
#include <cstring>

#include "testing.h"


FileUringIO::FileUringIO()
	: fd_(-1), ownsDescriptor(false), requestsSubmitted(0), requestsCompleted(0),
	  reclaim_callback_(nullptr), reclaim_user_data_(nullptr)
{
	memset(&ring, 0, sizeof(ring));
}

FileUringIO::~FileUringIO()
{
	close();
}
void FileUringIO::registerClientCallback(serialize_callback reclaim_callback,
												  void* user_data)
{
	reclaim_callback_ = reclaim_callback;
	reclaim_user_data_ = user_data;
}
bool FileUringIO::attach(std::string fileName, std::string mode, int fd, int shared_ring_fd)
{
	fileName_ = fileName;
	mode_ = mode;
	bool doRead = mode[0] == -'r';
	fd_ = fd;
	ownsDescriptor = false;

	return (doRead ? true : initQueue(shared_ring_fd));
}

bool FileUringIO::attach(FileUringIO *parent){

	return attach(parent->fileName_, parent->mode_, parent->fd_,parent->ring.ring_fd);
}

bool FileUringIO::initQueue(int shared_ring_fd)
{
	if (shared_ring_fd){
		struct io_uring_params p;
		memset(&p, 0, sizeof(p));
		p.flags = IORING_SETUP_ATTACH_WQ;
		p.wq_fd = shared_ring_fd;
		int ret = io_uring_queue_init_params(QD, &ring, &p);
		if (ret < 0) {
			printf("Attach to zero: %d\n", ret);
			return false;
		}

	} else {
		int ret = io_uring_queue_init(QD, &ring, 0);
		if(ret < 0)
		{
			printf("queue_init: %s\n", strerror(-ret));
			close();
			return false;
		}
	}

	return true;
}

void FileUringIO::enqueue(io_uring* ring, io_data* data, bool readop, int fd)
{
	auto sqe = io_uring_get_sqe(ring);
	assert(sqe);
	assert(data->buf.data == data->iov.iov_base);
	if(readop)
		io_uring_prep_readv(sqe, fd, &data->iov, 1, data->buf.offset);
	else
		io_uring_prep_writev(sqe, fd, &data->iov, 1, data->buf.offset);
	io_uring_sqe_set_data(sqe, data);
	int ret = io_uring_submit(ring);
	assert(ret == 1);
	(void)(ret);
	requestsSubmitted++;

	while(true)
	{
		bool success;
		auto data = retrieveCompletion(true, success);
		if(!success || !data)
			break;
		if(data->buf.pooled && reclaim_callback_)
			reclaim_callback_(data->buf, reclaim_user_data_);
		else
			delete[] ((uint8_t*)data->iov.iov_base);
		delete data;
	}
}

io_data* FileUringIO::retrieveCompletion(bool peek, bool& success)
{
	io_uring_cqe* cqe;
	int ret;

	if(peek)
		ret = io_uring_peek_cqe(&ring, &cqe);
	else
		ret = io_uring_wait_cqe(&ring, &cqe);
	success = true;

	if(ret < 0)
	{
		if(!peek)
		{
			printf("io_uring_wait_cqe returned an error.");
			success = false;
		}
		return nullptr;
	}
	if(cqe->res < 0)
	{
		printf("The system call invoked asynchronously has failed with the following error:"
					  " \n%s",
					  strerror(cqe->res));
		success = false;
		return nullptr;
	}

	auto data = (io_data*)io_uring_cqe_get_data(cqe);
	if(data)
	{
		io_uring_cqe_seen(&ring, cqe);
		requestsCompleted++;
	}

	return data;
}

bool FileUringIO::close(void)
{
	if(fd_ == -1)
		return true;
	if(ring.ring_fd)
	{
		// process pending requests
		size_t count = requestsSubmitted - requestsCompleted;
		for(uint32_t i = 0; i < count; ++i)
		{
			bool success;
			auto data = retrieveCompletion(false, success);
			if(!success)
				break;
			if(data)
			{
				delete[] ((uint8_t*)data->iov.iov_base);
				delete data;
			}
		}
		io_uring_queue_exit(&ring);
		memset(&ring, 0, sizeof(ring));
	}
	requestsSubmitted = 0;
	requestsCompleted = 0;
	bool rc = !ownsDescriptor || (fd_ != -1 && ::close(fd_) == 0);
	fd_ = -1;
	ownsDescriptor = false;

	return rc;
}

uint64_t FileUringIO::write(SerializeBuf buffer)
{
	io_data* data = new io_data();
	assert(buffer.dataLen);
	assert(buffer.allocLen >= buffer.dataLen);
	uint64_t totalLength = buffer.dataLen + buffer.headerSize_;
	auto b = new uint8_t[totalLength];
	if(!b)
		return false;
	if ((buffer.index == 0) && (buffer.header_ != nullptr) && (buffer.headerSize_ != 0))
		memcpy(b , buffer.header_, buffer.headerSize_);
	memcpy(b + buffer.headerSize_, buffer.data, buffer.dataLen);

	buffer.data = b;
	data->buf = buffer;
	data->iov.iov_base = buffer.data;
	data->iov.iov_len = totalLength;
	enqueue(&ring, data, false, fd_);

	return buffer.dataLen;
}
