cmake_minimum_required(VERSION 3.17)

project(ubench)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(CMAKE_MODULE_PATH
    ${CMAKE_SOURCE_DIR}/cmake
    ${CMAKE_MODULE_PATH})

find_package(Threads REQUIRED)

find_package(Valgrind)
message(STATUS "Looking for Valgrind")
if (Valgrind_FOUND)
	message(STATUS "Found Valgrind")
    add_definitions(-DHAVE_VALGRIND)
    include_directories(${Valgrind_INCLUDE_DIR})
endif(Valgrind_FOUND)

include (Findliburing)
include(GNUInstallDirs)

option(URING ON "Enable support for io_uring (requires liburing and Linux kernel >= 5.8)")
mark_as_advanced(URING)

#---Check for liburing----------------------------------------------------------------
if (URING)
  if(NOT CMAKE_SYSTEM_NAME MATCHES Linux)
    set(URING OFF CACHE BOOL "Disabled because liburing is only available on Linux" FORCE)
    message(STATUS "liburing was disabled : only available on Linux")
    set(UBENCH_HAVE_URING undef)
  else()
    message(STATUS "Looking for liburing")
    find_package(liburing)
    if(NOT LIBURING_FOUND)
      if(fail-on-missing)
        message(FATAL_ERROR "liburing not found and uring option required")
      else()
        message(STATUS "liburing not found. Switching off uring option")
        set(uring OFF CACHE BOOL "Disabled because liburing was not found (${uring_description})" FORCE)
      endif()
    else()
    	message(STATUS "Found liburing") 
    	set(UBENCH_HAVE_URING define)
    endif()
  endif()
endif()

add_subdirectory(thirdparty)

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/
  ${CMAKE_CURRENT_SOURCE_DIR}/src/include
  ${TIFF_INCLUDE_DIRNAME})
  
set(LIBRARY_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/FileUringIO.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Serializer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ImageFormat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/TIFFFormat.cpp
  )
  
configure_file(
 ${CMAKE_CURRENT_SOURCE_DIR}/src/ubench_config.h.cmake.in
 ${CMAKE_CURRENT_BINARY_DIR}/src/ubench_config.h
 @ONLY
 )

add_executable(ubench ${CMAKE_CURRENT_SOURCE_DIR}/src/ubench.cpp  ${LIBRARY_SRCS} )

target_link_libraries(ubench ${TIFF_LIBNAME} uring ${CMAKE_THREAD_LIBS_INIT} )
