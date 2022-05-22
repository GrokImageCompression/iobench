/*
 *    Copyright (C) 2022 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifdef _WIN32
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#endif
#include <cstring>
#include <cassert>

#include "FileIOUnix.h"

namespace io {

FileIOUnix::FileIOUnix(uint32_t threadId, bool flushOnClose) :
	  FileIO(threadId, flushOnClose),
#ifdef IOBENCH_HAVE_URING
	  uring(threadId),
#endif
	  fd_(invalid_fd),
	  ownsFileDescriptor_(false)
{
}
FileIOUnix::~FileIOUnix(void){
	close();
}
void FileIOUnix::registerReclaimCallback(io_callback reclaim_callback,
												 void* user_data)
{
	FileIO::registerReclaimCallback(reclaim_callback, user_data);
#ifdef IOBENCH_HAVE_URING
	uring.registerReclaimCallback(reclaim_callback, user_data);
#endif
}
bool FileIOUnix::attach(FileIOUnix *parent){
	fd_ = parent->fd_;

#ifdef IOBENCH_HAVE_URING
	return uring.attach(&parent->uring);
#else
	return true;
#endif
}
int FileIOUnix::getMode(std::string mode)
{
	int m = -1;
#ifdef _WIN32

#else
	switch(mode[0])
	{
		case 'r':
			m = O_RDONLY;
			if(mode[1] == '+')
				m = O_RDWR;
			break;
		case 'w':
			m = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef __linux__
			if (mode[1] == 'd')
				m |= O_DIRECT;
#endif
			break;
		case 'a':
			m = O_WRONLY | O_CREAT;
			break;
		default:
			printf("Bad mode %s\n", mode.c_str());
			break;
	}
#endif

	return m;
}

bool FileIOUnix::open(std::string name, std::string mode, bool asynch)
{
	(void)asynch;
	if (!close())
		return false;
#ifdef _WIN32

#else
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
#ifdef __APPLE__
	if (mode[1] == 'd')
		fcntl(fd, F_NOCACHE, 1);
#elif defined(IOBENCH_HAVE_URING)
	if (asynch && !uring.attach(name, mode, fd,0))
		return false;
#endif
	fd_ = fd;
	filename_ = name;
	mode_ = mode;
	ownsFileDescriptor_ = true;
#endif

	return true;
}
bool FileIOUnix::close(void)
{
#ifdef _WIN32
	return true;
#else
#ifdef IOBENCH_HAVE_URING
	uring.close();
#endif
	int rc = 0;
	if (ownsFileDescriptor_) {
		if(fd_ == invalid_fd)
			return true;

		if (flushOnClose_){
			int fret = fsync(fd_);
			(void)fret;
			assert(!fret);
			//todo: check return value
		}
		rc = ::close(fd_);
		fd_ = invalid_fd;
	}
	ownsFileDescriptor_ = false;

	return rc == 0;
#endif
}
uint64_t FileIOUnix::seek(int64_t off, int32_t whence)
{
	if (simulateWrite_)
		return off_;
#ifdef _WIN32


#else
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
#endif
}
uint64_t FileIOUnix::write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers){
	if (!buffers || !numBuffers)
		return 0;
	uint64_t bytesWritten = 0;
#ifdef _WIN32

#else

#ifdef IOBENCH_HAVE_URING
	if (uring.active())
		return uring.write(offset, buffers, numBuffers);
#endif

	auto io = new IOScheduleData(offset,buffers,numBuffers);
	ssize_t writtenInCall = 0;
	for(; bytesWritten < io->totalBytes_; bytesWritten += (uint64_t)writtenInCall)
	{
		writtenInCall = pwritev(fd_, (const iovec*)io->iov_, (int32_t)numBuffers, (int64_t)offset);
		if(writtenInCall <= 0)
			break;
	}
	delete io;
#endif

	for (uint32_t i = 0; i < numBuffers; ++i){
		auto b = buffers[i];
		assert(reclaim_callback_);
		reclaim_callback_(threadId_,b, reclaim_user_data_);
	}
	return bytesWritten;
}
uint64_t FileIOUnix::write(uint8_t* buf, uint64_t bytes_total)
{
	if (simulateWrite_){
		// offset 0 write is for file header
		if (off_ != 0) {
			if(++numSimulatedWrites_ == maxSimulatedWrites_)
				simulateWrite_ = false;
		}
		off_ += bytes_total;
		return bytes_total;
	}
	uint64_t bytes_written = 0;
#ifdef _WIN32

#else
	ssize_t count = 0;
	for(; bytes_written < bytes_total; bytes_written += (uint64_t)count)
	{
		const char* buf_offset = (char*)buf + bytes_written;
		uint64_t io_size = (uint64_t)(bytes_total - bytes_written);
		count = ::write(fd_, buf_offset, io_size);
		if(count <= 0)
			break;
	}
#endif

	return bytes_written;
}

}
