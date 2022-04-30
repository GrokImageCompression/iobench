#pragma once

#include <cstdint>

struct Image{
	Image() : Image(0,0,0,0)
	{}
	Image(uint32_t width, uint32_t height, uint16_t numcomps, uint32_t rowsPerStrip) :
		width_(width), height_(height),numcomps_(numcomps), rowsPerStrip_(rowsPerStrip),
		numStrips_(rowsPerStrip ? (height  + rowsPerStrip - 1)/ rowsPerStrip : 0),
		stripPackedByteWidth_(numcomps * width),
		stripLen_(rowsPerStrip * stripPackedByteWidth_),
		finalStripHeight_(rowsPerStrip ? height - ((height / rowsPerStrip) * rowsPerStrip) : 0),
		finalStripLen_ (finalStripHeight_ * stripPackedByteWidth_)
	{
	}
	uint32_t width_;
	uint32_t height_;
	uint16_t numcomps_;
	uint32_t rowsPerStrip_;
	uint32_t numStrips_;
	uint64_t stripPackedByteWidth_;
	uint64_t stripLen_;
	uint32_t finalStripHeight_;
	uint64_t finalStripLen_;

};
