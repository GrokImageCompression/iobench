### IOBench

`iobench` is a benchmark for testing performance on multithreaded
writes of large tiff files for synchronous and asynchronous
APIs. The benchmark works by assigning each
tiff strip (32 lines per strip) to a thread in a thread pool,
which does some work on the strip and then writes it out to disk.

While `libtiff` is used to write the header and directory data,
the actual pixel data is written outside of the
library, using either `uring` for asynchronous writes
or `pwritev` for synchronous writes.

### Dependencies

1. C++ compiler supporting at least `C++17`
2. [liburing](https://github.com/axboe/liburing)

### Command Line

If no command line arguments are used, then `iobench`
will cycle through all even concurrency levels from 2
to maximum concurrency and run five tests for each level :

1. no write
1. synchronous write
1. asynchronous write
1. synchronous direct write (using O_DIRECT)
1. asynchronous direct write (using O_DIRECT)

Note: square brackets below are used for clarity only


`-w, -width [width]`

Width of stored image.
 Default : `88000`

`-e, -height [height]`

Height of stored image.
Default : `32005`

`-s, -synchronous`

Synchronous writes using `pwritev`.
Default : `false`

`-k, -chunked`

Break each strip into chunks of size 64K, aligned on 512 byte
boundaries. Default: `false`

`-d, -direct`

Direct writes using `O_DIRECT`. This flag will automatically
enable chunk mode. Default: `false`

`-c, -concurrency [number of threads]`

Number of threads to use.
Default: `maximum concurrency` of system

`-f, -file [file name]`

Output file name
Default: `io_out.tif`
