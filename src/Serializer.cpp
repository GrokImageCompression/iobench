#include "Serializer.h"

#define IO_MAX 2147483647U

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>

static bool applicationReclaimCallback(io_buf *buffer, void* io_user_data)
{
	auto pool = (IBufferPool*)io_user_data;
	if(pool)
		pool->put(IOBuf(buffer));

	return true;
}

Serializer::Serializer(bool lockedPool) :
	  fd_(invalid_fd),
	  numPooledRequests_(0), maxPooledRequests_(0), off_(0),
	  reclaim_callback_(nullptr), reclaim_user_data_(nullptr),
	  pool_((IBufferPool*)(new BufferPool())),
	  ownsFileDescriptor_(false), simulateWrite_(false)
{
}
Serializer::~Serializer(void){
	close();
	delete pool_;
}
void Serializer::setMaxPooledRequests(uint32_t maxRequests)
{
	maxPooledRequests_ = maxRequests;
}
void Serializer::registerApplicationClient(void)
{
	registerClientCallback(applicationReclaimCallback, pool_);
}
void Serializer::registerClientCallback(io_callback reclaim_callback,
												 void* user_data)
{
	reclaim_callback_ = reclaim_callback;
	reclaim_user_data_ = user_data;
	uring.registerClientCallback(reclaim_callback, user_data);
}
IOBuf Serializer::getPoolBuffer(uint64_t len){
	return pool_->get(len);
}
IBufferPool* Serializer::getPool(void){
	return pool_;
}
bool Serializer::attach(Serializer *parent){
	fd_ = parent->fd_;

	return uring.attach(&parent->uring);
}
int Serializer::getMode(std::string mode)
{
	int m = -1;

	switch(mode[0])
	{
		case 'r':
			m = O_RDONLY;
			if(mode[1] == '+')
				m = O_RDWR;
			break;
		case 'w':
			m = O_WRONLY | O_CREAT | O_TRUNC;
			if (mode[1] == 'd')
				m |= O_DIRECT;
			break;
		case 'a':
			m = O_WRONLY | O_CREAT;
			break;
		default:
			printf("Bad mode %s\n", mode.c_str());
			break;
	}

	return m;
}

bool Serializer::open(std::string name, std::string mode, bool asynch)
{
	if (!close())
		return false;
	bool doRead = mode[0] == 'r';
	int fd = 0;
	int m = getMode(mode);
	if(m == -1)
		return false;
	fd = ::open(name.c_str(), m, 0666);
	if(fd < 0)
	{
		if(errno > 0 && strerror(errno) != NULL)
			printf("%s: %s\n", name.c_str(), strerror(errno));
		else
			printf("Cannot open %s\n", name.c_str());
		return false;
	}
	if (asynch && !uring.attach(name, mode, fd,0))
		return false;
	fd_ = fd;
	filename_ = name;
	mode_ = mode;
	ownsFileDescriptor_ = true;

	return true;
}
bool Serializer::close(void)
{
	uring.close();
	int rc = 0;
	if (ownsFileDescriptor_) {
		if(fd_ == invalid_fd)
			return true;

		rc = ::close(fd_);
		fd_ = invalid_fd;
	}
	ownsFileDescriptor_ = false;

	return rc == 0;
}
uint64_t Serializer::seek(int64_t off, int32_t whence)
{
	if (simulateWrite_)
		return off_;
	off_t rc = lseek(fd_, off, whence);
	if(rc == -1)
	{
		if(strerror(errno) != NULL)
			printf("%s\n", strerror(errno));
		else
			printf("I/O error\n");
		return (uint64_t)-1;
	}

	return (uint64_t)rc;
}
void Serializer::enableSimulateWrite(void){
	simulateWrite_ = true;
}
uint64_t Serializer::write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers){
	if (!buffers || !numBuffers)
		return 0;

	if (uring.active())
		return uring.write(offset, buffers, numBuffers);

	auto io = new IOScheduleData(offset,buffers,numBuffers);
	ssize_t count = 0;
	uint64_t bytesWritten = 0;
	for(; bytesWritten < io->totalBytes_; bytesWritten += (uint64_t)count)
	{
		uint64_t bytesRemaining = (uint64_t)(io->totalBytes_ - bytesWritten);
		count = pwritev(fd_, io->iov_, numBuffers, offset);
		if(count <= 0)
			break;
	}
	delete io;

	for (uint32_t i = 0; i < numBuffers; ++i){
		auto b = buffers[i];
		if (b->pooled){
			assert(reclaim_callback_);
			reclaim_callback_(b, reclaim_user_data_);
		}
	}
	return bytesWritten;
}
uint64_t Serializer::write(uint8_t* buf, uint64_t bytes_total)
{
	if (simulateWrite_){
		// offset 0 write is for file header
		if (off_ != 0) {
			if(++numPooledRequests_ == maxPooledRequests_)
				simulateWrite_ = false;
		}
		off_ += bytes_total;
		return bytes_total;
	}
	ssize_t count = 0;
	uint64_t bytes_written = 0;
	for(; bytes_written < bytes_total; bytes_written += (uint64_t)count)
	{
		const char* buf_offset = (char*)buf + bytes_written;
		uint64_t io_size = (uint64_t)(bytes_total - bytes_written);
		if(io_size > IO_MAX)
			io_size = IO_MAX;
		count = ::write(fd_, buf_offset, io_size);
		if(count <= 0)
			break;
	}

	return bytes_written;
}
