#pragma once

#include <cstdint>
#include <cassert>

typedef struct _serialize_buf
{
	uint8_t* data;
	uint64_t skip;
	uint64_t offset;
	uint64_t dataLen;
	uint64_t allocLen;
	bool pooled;
	uint32_t index;
} serialize_buf;

typedef bool (*serialize_callback)(serialize_buf buffer, void* serialize_user_data);
typedef void (*serialize_register_client_callback)(serialize_callback reclaim_callback,
													   void* serialize_user_data,
													   void* reclaim_user_data);
typedef bool (*serialize_pixels_callback)(serialize_buf buffer, void* user_data);
