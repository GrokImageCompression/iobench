#pragma once

#include <cstdint>
#include <cassert>

typedef struct _io_buf
{
	uint32_t index;
	uint64_t skip;
	uint64_t offset;
	uint8_t* data;
	uint64_t dataLen;
	uint64_t allocLen;
} io_buf;

typedef bool (*io_callback)(io_buf *buffer, void* io_user_data);
typedef void (*io_register_client_callback)(io_callback reclaim_callback,
													   void* io_user_data,
													   void* reclaim_user_data);
