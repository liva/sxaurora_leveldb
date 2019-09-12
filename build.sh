#!/bin/sh -xe
docker build docker -t leveldb-build-base:ve
docker run --rm -it -v $PWD:$PWD -w $PWD leveldb-build-base:ve sh -c 'mkdir -p build && cd build && CC=/opt/nec/nosupport/llvm-ve/bin/clang CXX=/opt/nec/nosupport/llvm-ve/bin/clang++ AR=/opt/nec/nosupport/llvm-ve/bin/llvm-ar CFLAGS="--target=ve-linux" CXXFLAGS="--target=ve-linux" LDFLAGS="--target=ve-linux" cmake3 -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=ON .. && cmake3 --build .'

#docker run --rm -it -v $PWD:$PWD -w $PWD rocksdb-build-base:ve sh -c 'V=1 ROCKSDB_DISABLE_ZLIB=1 ROCKSDB_DISABLE_SNAPPY=1 ROCKSDB_DISABLE_ALIGNED_NEW=1 PORTABLE=1 CC=/opt/nec/nosupport/llvm-ve/bin/clang CXX=/opt/nec/nosupport/llvm-ve/bin/clang++ AR=/opt/nec/nosupport/llvm-ve/bin/llvm-ar CFLAGS="--target=ve-linux" CXXFLAGS="--target=ve-linux" LDFLAGS="--target=ve-linux -lvedio -static" DEBUG_LEVEL=0 make db_bench -j12'
#docker run --rm -it -v $PWD:$PWD -w $PWD rocksdb-build-base:ve sh -c 'V=1 ROCKSDB_DISABLE_ZLIB=1 ROCKSDB_DISABLE_SNAPPY=1 ROCKSDB_DISABLE_ALIGNED_NEW=1 PORTABLE=1 CC=/opt/nec/nosupport/llvm-ve/bin/clang CXX=/opt/nec/nosupport/llvm-ve/bin/clang++ AR=/opt/nec/nosupport/llvm-ve/bin/llvm-ar CFLAGS="--target=ve-linux" CXXFLAGS="--target=ve-linux" LDFLAGS="--target=ve-linux -lvedio" DEBUG_LEVEL=0 make static_lib -j12'
#docker run -d --name rocksdb -it -v $PWD:$PWD -w $PWD rocksdb-build-base:ve sh
#docker exec -it rocksdb mkdir workdir
#docker exec -it rocksdb python -c 'import extract_archive; print extract_archive.extract_archive("librocksdb.a", "workdir")'
#docker exec -it rocksdb python -c 'import extract_archive; print extract_archive.extract_archive("/opt/nec/ve/lib/libvedio.a", "workdir")'
#docker exec -it rocksdb /opt/nec/nosupport/llvm-ve/bin/llvm-ar rcs /opt/nec/ve/lib/librocksdb.a workdir/*.o
#docker exec -it rocksdb rm -r workdir
#docker exec -it rocksdb sh -c 'cp db_bench /usr/local/bin && cp -r include/* /opt/nec/ve/include'
#docker commit rocksdb rocksdb:ve
#docker rm -f rocksdb

