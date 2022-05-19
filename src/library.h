#pragma once

#include <cstdint>

typedef struct _io_buf
{
	uint32_t index_;
	uint64_t skip_;
	uint64_t offset_;
	uint8_t* data_;
	uint64_t len_;
	uint64_t allocLen_;
} io_buf;

typedef bool (*io_callback)(io_buf *buffer, void* io_user_data);
typedef void (*io_register_client_callback)(io_callback reclaim_callback,
													   void* io_user_data,
													   void* reclaim_user_data);
