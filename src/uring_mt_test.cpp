#include <taskflow/taskflow.hpp>
#include "TIFFFormat.h"
#include "SeamCache.h"
#include "timer.h"
#include <cstdlib>

static void run(uint32_t concurrency, bool doStore, bool doAsynch){
	TIFFFormat tiffFormat;
	Image img(88000, 32005,1,32);
   auto headerInfo = tiffFormat.getHeaderInfo();
   if (doStore)
	   tiffFormat.encodeInit(img, "dump.tif",
			   doAsynch ? SERIALIZE_STATE_ASYNCH_WRITE : SERIALIZE_STATE_SYNCH,concurrency);

    printf("Run with concurrency = %d, store to disk = %d, use uring = %d\n",concurrency,doStore,doAsynch);
	tf::Executor exec(concurrency);
	tf::Taskflow taskflow;
	auto tasks = new tf::Task[img.numStrips_];
	for(uint64_t i = 0; i < img.numStrips_; i++)
		tasks[i] = taskflow.placeholder();
	for(uint16_t j = 0; j < img.numStrips_; ++j)
	{
		uint16_t strip = j;
		tasks[j].work([&tiffFormat, strip,doStore,img,headerInfo, &exec] {
			uint64_t len =  (strip == img.numStrips_ - 1) ? img.finalStripLen_ : img.stripLen_;
			uint8_t b[img.stripLen_] __attribute__((__aligned__(ALIGNMENT)));
			for (uint64_t k = 0; k < img.rowsPerStrip_ * 16 * 1024; ++k)
				b[k%len] = k;
			if (doStore)
				tiffFormat.encodePixels(exec.this_worker_id(),  b, img.stripLen_ * strip, len, strip);
		});
	}
	ChronoTimer timer;
	timer.start();
	exec.run(taskflow).wait();
	timer.finish(doAsynch ? "time to schedule" : "");
	if (doAsynch) {
		timer.start();
		tiffFormat.close();
		timer.finish("time to flush");
	}else {
		tiffFormat.close();
	}
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
