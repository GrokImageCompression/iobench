#include <taskflow/taskflow.hpp>
#include "TIFFFormat.h"
#include "timer.h"

static void run(uint32_t concurrency, bool doStore, bool doAsynch){
	   TIFFFormat tiffFormat;
	   Image img;
	   img.width_ = 88000;
	   img.height_ = 32000;
	   img.numcomps_ = 1;
	   img.rowsPerStrip_ = 32;
	   uint32_t numStrips = (img.height_ + img.rowsPerStrip_ - 1) / img.rowsPerStrip_;
	   uint64_t len = img.width_ * img.rowsPerStrip_ * img.numcomps_;

	   if (doStore)
		   tiffFormat.encodeInit(img, "dump.tif", doAsynch);

	   printf("Run with concurrency = %d, store to disk = %d, use uring = %d\n",concurrency,doStore,doAsynch);

		tf::Executor exec(concurrency);
		tf::Taskflow taskflow;
		auto tasks = new tf::Task[numStrips];
		for(uint64_t i = 0; i < numStrips; i++)
			tasks[i] = taskflow.placeholder();
		for(uint16_t j = 0; j < numStrips; ++j)
		{
			uint16_t tileIndex = j;
			tasks[j].work([&tiffFormat, tileIndex,len,doStore,img] {
				auto b = new uint8_t[len];
				for (uint64_t k = 0; k < img.rowsPerStrip_ * 16 * 1024; ++k)
					b[k%len] = k;
				if (doStore)
					tiffFormat.encodePixels(b, len, tileIndex);
				delete[] b;
			});
		}
		ChronoTimer timer;
		timer.start();
		exec.run(taskflow).wait();
		timer.finish("");
}

int main(int argc, char** argv)
{
	for (uint8_t i = 2; i <= std::thread::hardware_concurrency(); i+=2){
	   run(i,false,false);
	   run(i,true,false);
	   run(i,true,true);
	   printf("\\\\\\\\\\\\\\\\\\\\\\\\\\\n");
   }

   return 0;
}
