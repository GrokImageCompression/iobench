#include <taskflow/taskflow.hpp>
#include "TIFFFormat.h"
#include "timer.h"
#include <cstdlib>
#include "testing.h"

static void run(uint32_t width, uint32_t height,
		uint32_t concurrency, bool doStore, bool doAsynch){
	ChronoTimer timer;
	bool storeAsynch = doStore && doAsynch;
	{
		TIFFFormat tiffFormat;
		uint32_t packedBytesWidth = width * 1;
		ImageStripper img(width, height,1,32);
		if (doStore){
			std::string filename = "dump.tif";
			remove(filename.c_str());
		   tiffFormat.encodeInit(img, filename, doAsynch,concurrency);
		}

		printf("Run with concurrency = %d, store to disk = %d, use uring = %d\n",concurrency,doStore,doAsynch);
		tf::Executor exec(concurrency);
		tf::Taskflow taskflow;
		tf::Task* encodeStrips = new tf::Task[img.numStrips_];
		for (uint32_t strip = 0; strip < img.numStrips_; ++strip)
			encodeStrips[strip] = taskflow.placeholder();
		tf::Task* encodeSeams = new tf::Task[img.numStrips_-1];
		for (uint32_t seam = 0; seam < img.numStrips_-1; ++seam){
			encodeSeams[seam] = taskflow.placeholder();
			encodeStrips[seam].precede(encodeSeams[seam]);
			encodeStrips[seam+1].precede(encodeSeams[seam]);
		}
		for(uint32_t seam = 0; seam < img.numStrips_-1; ++seam)
		{
			encodeSeams[seam].work([] {
			});
		}
		for(uint32_t strip = 0; strip < img.numStrips_; ++strip)
		{
			uint32_t currentStrip = strip;
			encodeStrips[strip].work([&tiffFormat, currentStrip,doAsynch,doStore,img,&exec] {
				if (!doStore) {
					uint64_t len =  (currentStrip == img.numStrips_ - 1) ? img.finalStripLen_ : img.stripLen_;
					uint8_t b[img.stripLen_] __attribute__((__aligned__(ALIGNMENT)));
					for (uint64_t k = 0; k < 2*len; ++k)
						b[k/2] = k;
				} else {
					auto b = tiffFormat.getPoolBuffer(exec.this_worker_id(), currentStrip);
					auto ptr = b.data + b.skip;
					for (uint64_t k = 0; k < 2*(b.dataLen-b.skip); ++k)
						ptr[k/2] = k;
					bool ret = tiffFormat.encodePixels(exec.this_worker_id(),b);
					assert(ret);
				}
			});
		}
		timer.start();
		exec.run(taskflow).wait();
		delete[] encodeStrips;
		delete[] encodeSeams;
		if (storeAsynch){
			timer.finish("scheduling");
			timer.start();
		}
		tiffFormat.encodeFinish();
	}
	timer.finish(storeAsynch ? "flush" : "");
}
static void run(uint32_t width, uint32_t height,uint8_t concurrency){
	   run(width,height,concurrency,false,false);
	   run(width,height,concurrency,true,false);
	   run(width,height,concurrency,true,true);
	   printf("\\\\\\\\\\\\\\\\\\\\\\\\\\\n");
}

int main(int argc, char** argv)
{
	uint32_t width = 88000;
	uint32_t height = 32005;
	if (argc == 1) {
		for (uint8_t concurrency = 2;
				concurrency <= std::thread::hardware_concurrency(); concurrency+=2){
		   run(width,height,concurrency);
	   }
	} else {
		uint8_t concurrency = atoi(argv[1]);
		uint8_t useUring = 1;
		if (argc >= 3)
			useUring = atoi(argv[2]);
		if (concurrency > 0)
			run(width,height,concurrency, true, useUring);
		else
			run(width,height,std::thread::hardware_concurrency(),true,useUring);
	}

   return 0;
}
