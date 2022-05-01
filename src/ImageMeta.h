#pragma once

#include <cstdint>

struct ImageMeta{
	ImageMeta() : ImageMeta(0,0,0,0)
	{}
	ImageMeta(uint32_t width, uint32_t height, uint16_t numcomps, uint32_t nominalStripHeight) :
		width_(width), height_(height),numcomps_(numcomps), nominalStripHeight_(nominalStripHeight),
		numStrips_(nominalStripHeight ? (height  + nominalStripHeight - 1)/ nominalStripHeight : 0),
		stripPackedByteWidth_(numcomps * width),
		stripLen_(nominalStripHeight * stripPackedByteWidth_),
		finalStripHeight_( (nominalStripHeight && (height % nominalStripHeight != 0)) ?
							height - ((height / nominalStripHeight) * nominalStripHeight) :
								nominalStripHeight),
		finalStripLen_ (finalStripHeight_ * stripPackedByteWidth_)
	{}
	uint32_t stripHeight(uint32_t strip){
		return (strip < numStrips_-1) ? nominalStripHeight_ : finalStripHeight_;
	}
	uint64_t stripLen(uint32_t strip){
		return stripHeight(strip) * stripPackedByteWidth_;
	}
	uint64_t stripOffset(uint32_t strip){
		return strip * nominalStripHeight_ * stripPackedByteWidth_;
	}
	uint32_t width_;
	uint32_t height_;
	uint16_t numcomps_;
	uint32_t nominalStripHeight_;
	uint32_t numStrips_;
	uint64_t stripPackedByteWidth_;
	uint64_t stripLen_;
	uint32_t finalStripHeight_;
	uint64_t finalStripLen_;

};
