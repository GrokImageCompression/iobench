#include <taskflow/taskflow.hpp>
#include "TIFFFormat.h"
#include "SeamCache.h"
#include "timer.h"
#include <cstdlib>

static void run(uint32_t concurrency, bool doStore, bool doAsynch){
		uint32_t imgWidth = 88000;
		uint8_t numComps = 1;
	    TIFFFormat tiffFormat;
		auto headerInfo = tiffFormat.getHeaderInfo();
		SeamCacheInitInfo seamInit;
		seamInit.headerSize_ = headerInfo.length_;
		seamInit.nominalStripHeight_ = 32;
		seamInit.height_ = 32000;
		seamInit.stripPackedByteWidth_ = numComps * imgWidth;
		seamInit.writeSize_ = WRTSIZE;
		SeamCache seamCache(seamInit);

	   Image img;
	   img.width_ = imgWidth;
	   img.height_ = seamInit.height_;
	   img.numcomps_ = numComps;
	   img.rowsPerStrip_ = seamInit.nominalStripHeight_;

	   if (doStore)
		   tiffFormat.encodeInit(img, "dump.tif", doAsynch,concurrency);

	   printf("Run with concurrency = %d, store to disk = %d, use uring = %d\n",concurrency,doStore,doAsynch);

		tf::Executor exec(concurrency);
		tf::Taskflow taskflow;
		uint32_t numStrips = seamCache.getNumStrips();
		auto tasks = new tf::Task[numStrips];
		for(uint64_t i = 0; i < numStrips; i++)
			tasks[i] = taskflow.placeholder();
		uint64_t len = img.width_ * img.rowsPerStrip_ * img.numcomps_;
		for(uint16_t j = 0; j < numStrips; ++j)
		{
			uint16_t strip = j;
			tasks[j].work([&tiffFormat, strip,len,doStore,img, &seamCache] {
			    uint8_t b[len] __attribute__((__aligned__(ALIGNMENT)));
				for (uint64_t k = 0; k < img.rowsPerStrip_ * 16 * 1024; ++k)
					b[k%len] = k;
				if (strip == 0){
					auto headerInfo = tiffFormat.getHeaderInfo();
					memcpy(b,headerInfo.header_,headerInfo.length_);
				}
				if (doStore) {
					auto seamInfo = seamCache.getSeamInfo(strip);
					tiffFormat.encodePixels(b, seamInfo.lowerBegin_, len, strip);
				}
			});
		}
		ChronoTimer timer;
		timer.start();
		exec.run(taskflow).wait();
		timer.finish("");
}
static void run(uint8_t i){
	   run(i,false,false);
	   run(i,true,false);
	   run(i,true,true);
	   printf("\\\\\\\\\\\\\\\\\\\\\\\\\\\n");
}

int main(int argc, char** argv)
{
	if (argc == 1) {
		for (uint8_t i = 2; i <= std::thread::hardware_concurrency(); i+=2){
		   run(i);
	   }
	} else {
		uint8_t i = atoi(argv[1]);
		uint8_t j = 1;
		if (argc >= 3)
			j = atoi(argv[2]);
		if (i > 0)
			run(i, true, j != 0);
		else
			run(std::thread::hardware_concurrency(),true,j != 0);
	}

   return 0;
}
