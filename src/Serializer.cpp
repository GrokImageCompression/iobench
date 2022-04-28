#include "Serializer.h"
#define IO_MAX 2147483647U

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>

Serializer::Serializer(void)
	:
	  fd_(-1),
	  numPooledRequests_(0), maxPooledRequests_(0), asynchActive_(false), off_(0),
	  reclaim_callback_(nullptr), reclaim_user_data_(nullptr)
{}

void Serializer::setMaxPooledRequests(uint32_t maxRequests)
{
	maxPooledRequests_ = maxRequests;
}

void Serializer::serializeRegisterClientCallback(serialize_callback reclaim_callback,
												 void* user_data)
{
	reclaim_callback_ = reclaim_callback;
	reclaim_user_data_ = user_data;
	uring.serializeRegisterClientCallback(reclaim_callback, user_data);
}
serialize_callback Serializer::getSerializerReclaimCallback(void)
{
	return reclaim_callback_;
}
void* Serializer::getSerializerReclaimUserData(void)
{
	return reclaim_user_data_;
}
int Serializer::getFd(void)
{
	return fd_;
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
	return (m);
}

bool Serializer::open(std::string name, std::string mode, bool asynch)
{
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

	if (asynch) {
		if(!uring.attach(name, mode, fd))
			return false;
		asynchActive_ = true;
	}
	fd_ = fd;
	filename_ = name;
	mode_ = mode;

	return true;
}
bool Serializer::close(void)
{
	if(fd_ < 0)
		return true;

	int rc = ::close(fd_);
	fd_ = -1;

	return rc == 0;
}
uint64_t Serializer::seek(int64_t off, int32_t whence)
{
	if(asynchActive_)
		return off_;
	off_t rc = lseek(getFd(), off, whence);
	if(rc == (off_t)-1)
	{
		if(strerror(errno) != NULL)
			printf("%s", strerror(errno));
		else
			printf("I/O error");
		return (uint64_t)-1;
	}

	return (uint64_t)rc;
}
size_t Serializer::write(uint8_t* buf, size_t bytes_total)
{
	// asynch
	if(asynchActive_)
	{
#if 0
		struct TIFFFormatHeaderClassic {
			uint16_t tiff_magic;      /* magic number (defines byte order) */
			uint16_t tiff_version;    /* TIFF version number */
			uint32_t tiff_diroff;     /* byte offset to first directory */
		};
		if (off_ == 0 && bytes_total == 8){
			auto hdr = (TIFFFormatHeaderClassic*)buf;

			int k = 0 ;
		}
#endif

		scheduled_.data = buf;
		scheduled_.dataLen = bytes_total;
		scheduled_.offset = off_;
		uring.write(scheduled_);
		off_ += scheduled_.dataLen;
		if(scheduled_.pooled && (++numPooledRequests_ == maxPooledRequests_)){
			asynchActive_ = false;
			bool rc = uring.close();
			::close(fd_);
			fd_ = -1;
			rc = open(filename_,"a",false);
		}
		// clear
		scheduled_ = SerializeBuf();

		return bytes_total;
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

	if(!asynchActive_ && scheduled_.pooled)
	   ++numPooledRequests_;

	return (size_t)count;
}

void Serializer::initPooledRequest(void)
{
	scheduled_.pooled = true;
}
uint32_t Serializer::getNumPooledRequests(void)
{
	return numPooledRequests_;
}
uint64_t Serializer::getOffset(void)
{
	return off_;
}
bool Serializer::allPooledRequestsComplete(void)
{
	return numPooledRequests_ == maxPooledRequests_;
}
