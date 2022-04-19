#pragma once

#include <cstdint>

struct Image{
	Image() : width_(0), height_(0),numcomps_(0), rowsPerStrip_(0)
	{}
	uint32_t width_;
	uint32_t height_;
	uint8_t numcomps_;
	uint32_t rowsPerStrip_;
};
