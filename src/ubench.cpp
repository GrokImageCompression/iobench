#include <taskflow/taskflow.hpp>
#include "TIFFFormat.h"
#include "timer.h"
#include <cstdlib>
#include "testing.h"
#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"

static void run(uint32_t width, uint32_t height,bool direct,
		uint32_t concurrency, bool doStore, bool doAsynch){
	ChronoTimer timer;
	bool storeAsynch = doStore && doAsynch;
	{
		TIFFFormat tiffFormat;
		tiffFormat.init(width, height, 1, 32);
		auto imageStripper = tiffFormat.getImageStripper();
		if (doStore){
			std::string filename = "dump.tif";
			remove(filename.c_str());
		   tiffFormat.encodeInit(filename,direct,concurrency,doAsynch);
		}

		printf("Run with concurrency = %d, store to disk = %d, use uring = %d\n",concurrency,doStore,doAsynch);
		tf::Executor exec(concurrency);
		tf::Taskflow taskflow;
		tf::Task* encodeStrips = new tf::Task[imageStripper->numStrips()];
		for (uint32_t strip = 0; strip < imageStripper->numStrips(); ++strip)
			encodeStrips[strip] = taskflow.placeholder();
		for(uint32_t strip = 0; strip < imageStripper->numStrips(); ++strip)
		{
			uint32_t currentStrip = strip;
			encodeStrips[strip].work([&tiffFormat, currentStrip,doAsynch,doStore,imageStripper,&exec] {
				if (!doStore) {
					auto strip = imageStripper->getStrip(currentStrip);
					uint64_t len =  strip->len_;
					uint8_t b[len] __attribute__((__aligned__(ALIGNMENT)));
					for (uint64_t k = 0; k < 2*len; ++k)
						b[k/2] = k;
				} else {
					bool writeChunks = false;
					if (writeChunks) {
						// iterate through all blocks, allocate memory, write to memory,
						// and submit to disk
						StripChunkBuffer *chunkBuffer = nullptr;
						while (tiffFormat.nextChunk(exec.this_worker_id(), currentStrip, &chunkBuffer)){
							auto ptr = chunkBuffer->data();
							assert(ptr);
							memset(ptr + chunkBuffer->writeableOffset_, 0, chunkBuffer->writeableLen_);

							bool ret = tiffFormat.submit(exec.this_worker_id(), chunkBuffer);
							assert(ret);
						}
					} else {
						auto b = tiffFormat.getPoolBuffer(exec.this_worker_id(), currentStrip);
						auto ptr = b.data + b.skip;
						for (uint64_t k = 0; k < 2*(b.dataLen-b.skip); ++k)
							ptr[k/2] = k;
						bool ret = tiffFormat.encodePixels(exec.this_worker_id(),b);
						assert(ret);
					}

				}
			});
		}
		timer.start();
		exec.run(taskflow).wait();
		delete[] encodeStrips;
		if (storeAsynch){
			timer.finish("scheduling");
			timer.start();
		}
		tiffFormat.encodeFinish();
	}
	timer.finish(storeAsynch ? "flush" : "");
}
static void run(uint32_t width, uint32_t height,bool direct,uint8_t concurrency){
	   run(width,height,direct,concurrency,false,false);
	   run(width,height,direct,concurrency,true,false);
	   run(width,height,direct,concurrency,true,true);
	   printf("\\\\\\\\\\\\\\\\\\\\\\\\\\\n");
}

int main(int argc, char** argv)
{
	uint32_t width = 88000;
	uint32_t height = 32005;
	uint32_t concurrency = 0;
	bool useUring = true;
	bool fullRun = true;
	bool direct = false;
	try
	{
		TCLAP::CmdLine cmd("uring test bench command line", ' ', "1.0");

		TCLAP::ValueArg<uint32_t> widthArg("w", "width",
												  "image width",
												  false, 0, "unsigned integer", cmd);
		TCLAP::ValueArg<uint32_t> heightArg("e", "height",
												  "image height",
												  false, 0, "unsigned integer", cmd);
		TCLAP::SwitchArg synchArg("s", "synchronous", "synchronous writes", cmd);
		TCLAP::SwitchArg directArg("d", "direct", "use O_DIRECT", cmd);
		TCLAP::ValueArg<uint32_t> concurrencyArg("c", "concurrency",
												  "concurrency",
												  false, 0, "unsigned integer", cmd);
		cmd.parse(argc, argv);

		if (widthArg.isSet())
			width = widthArg.getValue();
		if (heightArg.isSet())
			height = heightArg.getValue();
		direct = directArg.isSet();
		if (concurrencyArg.isSet()) {
			concurrency = concurrencyArg.getValue();
			fullRun = false;
		}
		if (synchArg.isSet())
			useUring = false;
	}
	catch(TCLAP::ArgException& e) // catch any exceptions
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}
	if (fullRun) {
		for (uint8_t concurrency = 2;
				concurrency <= std::thread::hardware_concurrency(); concurrency+=2){
		   run(width,height,direct,concurrency);
	   }
	} else {
		if (concurrency > 0)
			run(width,height,direct, concurrency, true, useUring);
		else
			run(width,height,direct,std::thread::hardware_concurrency(),true,useUring);
	}

   return 0;
}
