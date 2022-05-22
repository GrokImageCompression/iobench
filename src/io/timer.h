/*
 *    Copyright (C) 2022 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */



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
