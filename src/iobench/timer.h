#pragma once

#include <chrono>
#include <string>

namespace iobench {


class ChronoTimer {
public:
	ChronoTimer(void) {
	}
	void start(void){
		startTime = std::chrono::high_resolution_clock::now();
	}
	void finish(std::string msg){
		auto finish = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = finish - startTime;
		printf("%s : %f ms\n",msg.c_str(), elapsed.count() * 1000);
	}
private:
	std::chrono::high_resolution_clock::time_point startTime;
};

}
