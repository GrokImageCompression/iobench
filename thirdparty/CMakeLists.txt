message(STATUS "libtiff will be built to support TIFF files.")
add_subdirectory(libtiff EXCLUDE_FROM_ALL)
set(TIFF_LIBNAME tiff PARENT_SCOPE)
set(TIFF_INCLUDE_DIRNAME
  ${CMAKE_SOURCE_DIR}/thirdparty/libtiff
  ${CMAKE_bin_DIR}/thirdparty/libtiff
  PARENT_SCOPE)

