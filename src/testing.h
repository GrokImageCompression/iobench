#pragma once

#include <sstream>

#if defined(HAVE_VALGRIND)
#include <valgrind/memcheck.h>

const size_t valgrind_mem_ok = (size_t)-1;

//#define DEBUG_VALGRIND
template<typename T>
size_t valgrind_memcheck(const T* buf, size_t len)
{
	size_t val = VALGRIND_CHECK_MEM_IS_DEFINED(buf, len * sizeof(T));
	return (val) ? (val - (uint64_t)buf) / sizeof(T) : valgrind_mem_ok;
}
template<typename T>
bool valgrind_memcheck_all(const T* buf, size_t len, std::string msg)
{
	bool rc = true;
	for(uint32_t i = 0; i < len; ++i)
	{
		auto val = valgrind_memcheck<T>(buf + i, 1);
		if(val != valgrind_mem_ok)
		{
			std::ostringstream ss;
			ss << msg << " " << "offset = " << i + val << std::endl;
			printf("%s",ss.str().c_str());
			rc = false;
		}
	}
	return rc;
}

#endif
