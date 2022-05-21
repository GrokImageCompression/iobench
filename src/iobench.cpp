#include <cstdlib>

#include "iobench_config.h"
#include <taskflow/taskflow.hpp>
#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"

#include "iobench/TIFFFormat.h"
#include "iobench/timer.h"
#include "iobench/testing.h"

const uint8_t numStrips = 32;

namespace iobench {

static void run(uint32_t width, uint32_t height, uint8_t numComps, bool direct,
		uint32_t concurrency, bool doStore, bool doAsynch, bool chunked){
#ifndef IOBENCH_HAVE_URING
	if (doAsynch) {
		printf("Uring not enabled - forcing synchronous write.\n");
		doAsynch = false;
	}
#endif
	ChronoTimer timer;
	bool storeAsynch = doStore && doAsynch;
	auto tiffFormat = new TIFFFormat(true);
	tiffFormat->init(width, height, numComps,width * numComps, numStrips, chunked);
	auto imageStripper = tiffFormat->getImageStripper();
	if (doStore){
		std::string filename = "dump.tif";
		remove(filename.c_str());
	   tiffFormat->encodeInit(filename,direct,concurrency,doAsynch);
	}

	printf("Run with concurrency = %d, store to disk = %d, use uring = %d\n",
			concurrency,doStore,doAsynch);
	tf::Executor exec(concurrency);
	tf::Taskflow taskflow;
	tf::Task* encodeStrips = new tf::Task[imageStripper->numStrips()];
	for (uint32_t strip = 0; strip < imageStripper->numStrips(); ++strip)
		encodeStrips[strip] = taskflow.placeholder();
	for(uint32_t strip = 0; strip < imageStripper->numStrips(); ++strip)
	{
		uint32_t currentStrip = strip;
		encodeStrips[strip].work([&tiffFormat, chunked,
								  currentStrip,doAsynch,doStore,imageStripper,&exec] {
			auto strip = imageStripper->getStrip(currentStrip);
			if (!doStore) {
				uint64_t len =  strip->logicalLen_;
#ifdef _WIN32
				uint8_t *b = IOBuf::alignedAlloc(ALIGNMENT,len);
				for (uint64_t k = 0; k < len; ++k)
					b[k] = k%256;
				free(b);
#else
				uint8_t b[len] __attribute__((__aligned__(ALIGNMENT)));
				for (uint64_t k = 0; k < len; ++k)
					b[k] = k%256;
#endif
			} else {
				if (chunked) {
					auto chunkArray =
							tiffFormat->getStripChunkArray(exec.this_worker_id(),
									currentStrip);
					uint64_t val = chunkArray->stripChunks_[0]->offset();
					val += chunkArray->stripChunks_[0]->writeableOffset_;
					for (uint32_t i = 0; i < chunkArray->numBuffers_; ++i){
						auto ch = chunkArray->stripChunks_[i];
						auto b = chunkArray->ioBufs_[i];
						auto ptr = b->data_;
						assert(ptr);
						ptr += ch->writeableOffset_;
						for (uint64_t j = 0; j < ch->writeableLen_; ++j)
							ptr[j] = (val++)%256;
#ifdef DEBUG_VALGRIND
						if (!valgrind_memcheck_all(b->data_, b->len_,""))
							printf("Uninitialized memory in strip %d, "
									"buffer %d / %d, "
									"writeable len %d "
									"length %d\n",
									currentStrip,
									i+1,
									chunkArray->numBuffers_,
									ch->writeableLen_,
									ch->len());
#endif
					}
					bool ret = tiffFormat->encodePixels(exec.this_worker_id(), chunkArray);
					assert(ret);
					delete chunkArray;
				} else {
					auto b = tiffFormat->getPoolBuffer(exec.this_worker_id(), currentStrip);
					auto ptr = b->data_ + b->skip_;
					uint64_t val = b->offset_ + b->skip_;
					for (uint64_t k = 0; k < b->len_ - b->skip_; ++k)
						ptr[k] = (val++)%256;
					bool ret = tiffFormat->encodePixels(exec.this_worker_id(),&b,1);
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
	tiffFormat->encodeFinish();
	delete tiffFormat;
	timer.finish(storeAsynch ? "flush" : "");
}
static void run(uint32_t width, uint32_t height,uint8_t numComps,
		bool direct,uint8_t concurrency, bool chunked){
	   run(width,height,numComps,direct,concurrency,false,false,chunked);
	   run(width,height,numComps,direct,concurrency,true,false,chunked);
	   run(width,height,numComps,direct,concurrency,true,true,chunked);
	   printf("\\\\\\\\\\\\\\\\\\\\\\\\\\\n");
}

}

int main(int argc, char** argv)
{
	uint32_t width = 88000;
	uint32_t height = 32005;
	uint32_t numComps = 1;
	uint32_t concurrency = 0;
	bool useUring = true;
	bool fullRun = true;
	bool direct = false;
	bool chunked = false;
	try
	{
		TCLAP::CmdLine cmd("uring test bench command line", ' ', "1.0");

		TCLAP::ValueArg<uint32_t> widthArg("w", "width",
												  "image width",
												  false, 0, "unsigned integer", cmd);
		TCLAP::ValueArg<uint32_t> heightArg("e", "height",
												  "image height",
												  false, 0, "unsigned integer", cmd);
		TCLAP::ValueArg<uint32_t> numComponentsArg("n", "numcomps",
												  "number of components",
												  false, 0, "unsigned integer", cmd);
		TCLAP::SwitchArg synchArg("s", "synchronous", "synchronous writes", cmd);
		TCLAP::SwitchArg directArg("d", "direct", "use O_DIRECT", cmd);
		TCLAP::ValueArg<uint32_t> concurrencyArg("c", "concurrency",
												  "concurrency",
												  false, 0, "unsigned integer", cmd);
		TCLAP::SwitchArg chunkedArg("k", "chunked", "break strips into chunks", cmd);
		cmd.parse(argc, argv);

		if (widthArg.isSet())
			width = widthArg.getValue();
		if (heightArg.isSet())
			height = heightArg.getValue();
		if (numComponentsArg.isSet())
			numComps = numComponentsArg.getValue();
		if (directArg.isSet()){
#ifdef __linux__
			direct = directArg.isSet();
			chunked = true;
#else
		std::cout << "Direct IO not supported" << std::endl;
#endif
		}
		if (concurrencyArg.isSet()) {
			concurrency = concurrencyArg.getValue();
			fullRun = false;
		}
		if (synchArg.isSet())
			useUring = false;
		if (chunkedArg.isSet())
			chunked = true;
	}
	catch(TCLAP::ArgException& e) // catch any exceptions
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}
	if (fullRun) {
		for (uint8_t concurrency = 2;
				concurrency <= std::thread::hardware_concurrency(); concurrency+=2){
		   iobench::run(width,height,numComps,direct,concurrency,chunked);
	   }
	} else {
		if (concurrency > 0)
			iobench::run(width,height,numComps,direct, concurrency, true, useUring,chunked);
		else
			iobench::run(width,height,numComps,direct,std::thread::hardware_concurrency(),true,useUring,chunked);
	}

   return 0;
}
