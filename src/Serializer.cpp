#include "Serializer.h"

#define IO_MAX 2147483647U

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>

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
	  numPooledRequests_(0), maxPooledRequests_(0), state_(SERIALIZE_STATE_NONE), off_(0),
	  reclaim_callback_(nullptr), reclaim_user_data_(nullptr),
	  header_(nullptr), headerSize_(0)
{}
Serializer::~Serializer(void){
	close();
}
void Serializer::setHeader(uint8_t *header, uint32_t headerSize){
	header_ = header;
	headerSize_ = headerSize;
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
	state_ = parent->state_;
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
	return (m);
}

bool Serializer::open(std::string name, std::string mode, SerializeState serializeState)
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
	state_ = serializeState;
	if (state_ == SERIALIZE_STATE_ASYNCH_WRITE) {
		if(!uring.attach(name, mode, fd))
			return false;
	}
	fd_ = fd;
	filename_ = name;
	mode_ = mode;

	return true;
}
bool Serializer::close(void)
{
	if (state_ == SERIALIZE_STATE_ASYNCH_WRITE)
		uring.close();

	if(fd_ == invalid_fd)
		return true;

	int rc = ::close(fd_);
	fd_ = invalid_fd;

	return rc == 0;
}
uint64_t Serializer::seek(int64_t off, int32_t whence)
{
	if(state_ != SERIALIZE_STATE_SYNCH)
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

	return (uint64_t)rc;
}
size_t Serializer::write(uint8_t* buf, uint64_t offset, uint64_t size, uint32_t index){
	SerializeBuf ser;
	ser.data = buf;
	ser.dataLen = size;
	ser.offset = offset;
	ser.index = index;
	if (index == 0){
		ser.header_ = header_;
		ser.headerSize_ = headerSize_;
	}

	return uring.write(ser);
}
size_t Serializer::write(uint8_t* buf, uint64_t bytes_total)
{
	// asynch
	if(state_ != SERIALIZE_STATE_SYNCH)
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
			state_ = SERIALIZE_STATE_ASYNCH_WRITE;
			bool rc = uring.close();
			::close(fd_);
			fd_ = -1;
			rc = open(filename_,"a",SERIALIZE_STATE_SYNCH_SIM_PIXEL_WRITE);
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

	if(!state_ != SERIALIZE_STATE_SYNCH && scheduled_.pooled)
	   ++numPooledRequests_;

	return (size_t)count;
}
SerializeState Serializer::getState(void){
	return state_;
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
