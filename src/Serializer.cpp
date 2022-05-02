#include "Serializer.h"

#define IO_MAX 2147483647U

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>

bool debugAsynch = false;

static bool applicationReclaimCallback(serialize_buf buffer, void* serialize_user_data)
{
	auto pool = (BufferPool*)serialize_user_data;
	if(pool)
		pool->put(SerializeBuf(buffer));

	return true;
}

Serializer::Serializer(void)
	:
	  fd_(invalid_fd),
	  numPooledRequests_(0), maxPooledRequests_(0), asynch_(false), off_(0),
	  reclaim_callback_(nullptr), reclaim_user_data_(nullptr),
	  ownsFileDescriptor_(false)
{}
Serializer::~Serializer(void){
	close();
}
void Serializer::setMaxPooledRequests(uint32_t maxRequests)
{
	maxPooledRequests_ = maxRequests;
}
void Serializer::registerApplicationClient(void)
{
	registerClientCallback(applicationReclaimCallback, &pool_);
}
void Serializer::reclaimBuffer(serialize_buf buffer)
{
	if(reclaim_callback_)
		reclaim_callback_(buffer, reclaim_user_data_);
}
void Serializer::registerClientCallback(serialize_callback reclaim_callback,
												 void* user_data)
{
	reclaim_callback_ = reclaim_callback;
	reclaim_user_data_ = user_data;
	uring.registerClientCallback(reclaim_callback, user_data);
}
SerializeBuf Serializer::getPoolBuffer(uint64_t len){
	return pool_.get(len);
}
void Serializer::putPoolBuffer(SerializeBuf buf){
	pool_.put(buf);
}
bool Serializer::attach(Serializer *parent){
	fd_ = parent->fd_;
	asynch_ = parent->asynch_;

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
			printf("Bad mode %s", mode.c_str());
			break;
	}

	return m;
}

bool Serializer::open(std::string name, std::string mode, bool asynch)
{
	if (!close())
		return false;
	bool doRead = mode[0] == -'r';
	int fd = 0;

	int m = getMode(mode);
	if(m == -1)
		return false;

	fd = ::open(name.c_str(), m, 0666);
	if(fd < 0)
	{
		if(errno > 0 && strerror(errno) != NULL)
			printf("%s: %s", name.c_str(), strerror(errno));
		else
			printf("Cannot open %s", name.c_str());
		return false;
	}
	asynch_ = asynch;
	if (asynch_ && !uring.attach(name, mode, fd,0))
		return false;
	fd_ = fd;
	filename_ = name;
	mode_ = mode;
	ownsFileDescriptor_ = true;

	return true;
}
bool Serializer::close(void)
{
	if (asynch_)
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
	if (asynch_)
		return off_;
	off_t rc = lseek(fd_, off, whence);
	if(rc == (off_t)-1)
	{
		if(strerror(errno) != NULL)
			printf("%s", strerror(errno));
		else
			printf("I/O error");
		return (uint64_t)-1;
	}
	if (debugAsynch)
		fprintf(stderr,"seek to offset %d (actual offset %d)\n",off,rc);

	return (uint64_t)rc;
}
size_t Serializer::writeAsynch(SerializeBuf serializeBuf){
	return uring.write(serializeBuf);
}
size_t Serializer::write(uint8_t* buf, uint64_t bytes_total)
{
	// asynch
	if (asynch_){
		// offset 0 write is for file header
		if (off_ != 0) {
			if(++numPooledRequests_ == maxPooledRequests_)
				asynch_ = false;
		}
		if (debugAsynch)
			fprintf(stderr,"simulated write %d at offset %d : %ld\n",
							numPooledRequests_,bytes_total,off_);
		off_ += bytes_total;
		return bytes_total;
	}
	if (debugAsynch) {
		int pos = lseek(fd_, 0, SEEK_CUR);
		fprintf(stderr,"actual write %ld at offset %d\n",bytes_total, pos);
	}
	ssize_t count = 0;
	size_t bytes_written = 0;
	for(; bytes_written < bytes_total; bytes_written += (size_t)count)
	{
		const char* buf_offset = (char*)buf + bytes_written;
		size_t io_size = (size_t)(bytes_total - bytes_written);
		if(io_size > IO_MAX)
			io_size = IO_MAX;
		count = ::write(fd_, buf_offset, io_size);
		if(count <= 0)
			break;
	}
	if(scheduled_.pooled)
	   ++numPooledRequests_;

	return (size_t)count;
}
bool Serializer::isAsynch(void){
	return asynch_;
}
void Serializer::initPooledRequest(void)
{
	scheduled_.pooled = true;
}
bool Serializer::allPooledRequestsComplete(void)
{
	return numPooledRequests_ == maxPooledRequests_;
}
