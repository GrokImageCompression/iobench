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
bool FileUringIO::active(void){
	return ring.ring_fd != 0;
}
void FileUringIO::registerClientCallback(io_callback reclaim_callback,
												  void* user_data)
{
	reclaim_callback_ = reclaim_callback;
	reclaim_user_data_ = user_data;
}
bool FileUringIO::attach(std::string fileName, std::string mode, int fd, int shared_ring_fd)
{
	fileName_ = fileName;
	mode_ = mode;
	bool doRead = mode[0] == 'r';
	fd_ = fd;
	ownsDescriptor = false;

	return (doRead ? true : initQueue(shared_ring_fd));
}

bool FileUringIO::attach(FileUringIO *parent){
	if (!parent->active())
		return true;
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
			printf("io_uring_queue_init_params: %s\n", strerror(-ret));
			close();
			return false;
		}

	} else {
		int ret = io_uring_queue_init(QD, &ring, 0);
		if(ret < 0)
		{
			printf("io_uring_queue_init: %s\n", strerror(-ret));
			close();
			return false;
		}
	}

	return true;
}

void FileUringIO::enqueue(io_uring* ring, IOScheduleData* data, bool readop, int fd)
{
	auto sqe = io_uring_get_sqe(ring);
	assert(sqe);
	if(readop)
		io_uring_prep_readv(sqe, fd, data->iov_, data->numBuffers_, data->offset_);
	else
		io_uring_prep_writev(sqe, fd, data->iov_, data->numBuffers_, data->offset_);
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
		for (uint32_t i = 0; i < data->numBuffers_; ++i){
			auto b = data->buffers_[i];
			reclaim_callback_(b, reclaim_user_data_);
		}
		delete data;
	}
}

IOScheduleData* FileUringIO::retrieveCompletion(bool peek, bool& success)
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
		printf("Asynchronous system call failed with error:\n%s\n",
					  strerror(cqe->res));
		success = false;
		return nullptr;
	}

	auto data = (IOScheduleData*)io_uring_cqe_get_data(cqe);
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
				for (uint32_t j = 0; j < data->numBuffers_; ++j)
					data->buffers_[j]->dealloc();
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

uint64_t FileUringIO::write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers)
{
	auto data = new IOScheduleData(offset,buffers,numBuffers);
	uint64_t totalBytes = 0;
	for (uint32_t i = 0; i < numBuffers; ++i)
		totalBytes += buffers[i]->len_;
	enqueue(&ring, data, false, fd_);

	return totalBytes;
}
