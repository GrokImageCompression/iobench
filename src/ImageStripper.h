#pragma once

#include <cstdint>

struct Strip {
	Strip(void) : Strip(0,0,0)
	{}
	Strip(uint64_t offset,uint64_t len,uint32_t height) :
		offset_(offset), len_(len),height_(height)
	{}
	uint64_t offset_;
	uint64_t len_;
	uint32_t height_;
};

struct ImageStripper{
	ImageStripper() : ImageStripper(0,0,0,0)
	{}
	ImageStripper(uint32_t width, uint32_t height, uint16_t numcomps, uint32_t nominalStripHeight) :
		width_(width), height_(height),numcomps_(numcomps), nominalStripHeight_(nominalStripHeight),
		numStrips_(nominalStripHeight ? (height  + nominalStripHeight - 1)/ nominalStripHeight : 0),
		stripPackedByteWidth_(numcomps * width),
		stripLen_(nominalStripHeight * stripPackedByteWidth_),
		finalStripHeight_( (nominalStripHeight && (height % nominalStripHeight != 0)) ?
							height - ((height / nominalStripHeight) * nominalStripHeight) :
								nominalStripHeight),
		finalStripLen_ (finalStripHeight_ * stripPackedByteWidth_)
	{}
	Strip getStrip(uint32_t strip) const{
		return Strip(strip * nominalStripHeight_ * stripPackedByteWidth_,
					stripHeight(strip) * stripPackedByteWidth_,
					(strip < numStrips_-1) ? nominalStripHeight_ : finalStripHeight_);
	}
	uint32_t numStrips(void) const{
		return numStrips_;
	}
	uint32_t stripHeight(uint32_t strip) const{
		return (strip < numStrips_-1) ? nominalStripHeight_ : finalStripHeight_;
	}
	uint32_t width_;
	uint32_t height_;
	uint16_t numcomps_;
	uint32_t nominalStripHeight_;
private:
	uint64_t stripPackedByteWidth_;
	uint64_t stripLen_;
	uint32_t finalStripHeight_;
	uint64_t finalStripLen_;
	uint32_t numStrips_;
};
