### Ubench

`ubench` is a benchmark for testing `uring` performance on multithreaded
writes of large tiff files to disk. The test works by assigning each 
tiff strip (size 32 lines) to a thread in a thread pool, 
which does some work on the strip and then writes it out to disk.

While `libtiff` is used to write the header and directory data,
the actual pixel data is written outside of the
library, using either `uring` for asynchronous writes
or `pwritev` for synchronous writes.

### Command Line

If no command line arguments are used, then `ubench`
will cycle through all even concurrency levels from 2
to maximum concurrency and run three tests for each level :

1. no write
1. synchronous write
1. asynchronous write

\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\

Square brackets below are used for clarity only:


`-w, -width [width]`

Width of stored image.
 Default : `88000`

`-e, -height [height]`

Height of stored image.
Default : `32005`

`-s, -synchronous`

Synchronous writes using `pwritev`.
Default : `false`

`-d, -direct`

Direct writes using `O_DIRECT`. Should be used with
`-k` argument.
Default: `false`

`-c, -concurrency [number of threads]`

Number of threads to use.
Default: `maximum concurrency` of system

`-k, -chunked`

Break each strip into chunks of size 64K, aligned on 512 byte
boundaries. Default: `false`
