#include <taskflow/taskflow.hpp>
#include "TIFFFormat.h"
#include "timer.h"
#include <cstdlib>
#include "FlowComponent.h"
#include "testing.h"

static void run(uint32_t concurrency, bool doStore, bool doAsynch){
	ChronoTimer timer;
	{
		TIFFFormat tiffFormat;
		uint32_t width = 88000;
		uint32_t height = 32005;
		uint32_t packedBytesWidth = width * 1;
		ImageMeta img(width, height,1,32);
		if (doStore){
			std::string filename = "dump.tif";
			remove(filename.c_str());
		   tiffFormat.encodeInit(img, filename,
				   doAsynch ? SERIALIZE_STATE_ASYNCH_WRITE : SERIALIZE_STATE_SYNCH,concurrency);
		}

		printf("Run with concurrency = %d, store to disk = %d, use uring = %d\n",concurrency,doStore,doAsynch);
		tf::Executor exec(concurrency);
		tf::Taskflow taskflow;
		FlowComponent encodeFlow;
		encodeFlow.addTo(taskflow);
		for(uint32_t j = 0; j < img.numStrips_; ++j)
		{
			uint32_t strip = j;
			encodeFlow.nextTask().work([&tiffFormat, strip,doStore,img,&exec] {
				uint64_t len =  ((strip == img.numStrips_ - 1) && (img.finalStripLen_ != 0)) ?
									img.finalStripLen_ : img.stripLen_;
				assert(len);
				uint8_t b[img.stripLen_] __attribute__((__aligned__(ALIGNMENT)));
				for (uint64_t k = 0; k < 2*len; ++k)
					b[k/2] = k;
				if (doStore) {
					bool ret = tiffFormat.encodePixels(exec.this_worker_id(),  b, img.stripLen_ * strip, len, strip);
					assert(ret);
				}
			});
		}
		timer.start();
		exec.run(taskflow).wait();
		if (doStore && doAsynch){
			timer.finish("scheduling");
			timer.start();
		}
		tiffFormat.encodeFinish();
	}
	timer.finish(doStore && doAsynch ? "flush" : "");
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
