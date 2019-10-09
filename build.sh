#!/bin/sh -xe
docker build docker -t leveldb-build-base:ve
docker run --rm -it -v $PWD:$PWD -w $PWD leveldb-build-base:ve sh -c 'mkdir -p build && cd build && CC=/opt/nec/nosupport/llvm-ve/bin/clang CXX=/opt/nec/nosupport/llvm-ve/bin/clang++ AR=/opt/nec/nosupport/llvm-ve/bin/llvm-ar CFLAGS="--target=ve-linux" CXXFLAGS="--target=ve-linux" LDFLAGS="--target=ve-linux" cmake3 -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=ON .. && cmake3 --build . -j12'

docker run -d --name leveldb -it -v $PWD:$PWD -w $PWD leveldb-build-base:ve sh
#docker exec -it rocksdb mkdir workdir
#docker exec -it rocksdb python -c 'import extract_archive; print extract_archive.extract_archive("librocksdb.a", "workdir")'
#docker exec -it rocksdb python -c 'import extract_archive; print extract_archive.extract_archive("/opt/nec/ve/lib/libvedio.a", "workdir")'
#docker exec -it leveldb /opt/nec/nosupport/llvm-ve/bin/llvm-ar rcs /opt/nec/ve/lib/librocksdb.a workdir/*.o
#docker exec -it leveldb rm -r workdir
docker exec -it leveldb sh -c 'cp build/libleveldb.a /opt/nec/ve/lib/ && cp -r include/* /opt/nec/ve/include'
docker commit leveldb leveldb:ve
docker rm -f leveldb

