#!/bin/bash -xe
#
# Copyright 2020 NEC Laboratories Europe GmbH
# All rights reserved
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
# 
#    3. Neither the name of NEC Laboratories Europe GmbH nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY NEC Laboratories Europe GmbH AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NEC Laboratories 
# Europe GmbH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO,  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

sudo rm -rf build
echo "#ifndef SCRIPT_AUTOGEN_H_" > include/autogen.h
echo "#define SCRIPT_AUTOGEN_H_" >> include/autogen.h
for OPT in ${1}
do
    echo "#define ${OPT}" >> include/autogen.h
done
echo "#endif" >> include/autogen.h
docker run --rm -it -v $PWD:$PWD -w $PWD vefs:develop sh -c "mkdir -p build && cd build && CC=/opt/nec/nosupport/llvm-ve/bin/clang CXX=/opt/nec/nosupport/llvm-ve/bin/clang++ AR=/opt/nec/nosupport/llvm-ve/bin/llvm-ar CFLAGS='--target=ve-linux -g3' CXXFLAGS='--target=ve-linux -g3' LDFLAGS='--target=ve-linux -g' cmake3 -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=ON .. && cmake3 --build . -j12"

docker rm -f leveldb || :
docker run -d --name leveldb -it -v $PWD:$PWD -w $PWD vefs:develop sh
#docker exec -it rocksdb mkdir workdir
#docker exec -it rocksdb python -c 'import extract_archive; print extract_archive.extract_archive("librocksdb.a", "workdir")'
#docker exec -it rocksdb python -c 'import extract_archive; print extract_archive.extract_archive("/opt/nec/ve/lib/libvedio.a", "workdir")'
#docker exec -it leveldb /opt/nec/nosupport/llvm-ve/bin/llvm-ar rcs /opt/nec/ve/lib/librocksdb.a workdir/*.o
#docker exec -it leveldb rm -r workdir
docker exec -it leveldb sh -c 'cp build/libleveldb.a /opt/nec/ve/lib/ && cp -r include/* /opt/nec/ve/include'
docker commit leveldb leveldb:ve
docker rm -f leveldb
rm -rf include/autogen.h

