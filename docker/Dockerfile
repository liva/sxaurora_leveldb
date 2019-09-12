FROM unvme:ve

RUN yum install -y epel-release
RUN yum install -y git cmake3 make
RUN git clone https://github.com/gflags/gflags.git

WORKDIR gflags
RUN git checkout -b v2.2.2
RUN mkdir build
WORKDIR build
RUN cmake3 -DCMAKE_INSTALL_PREFIX="/opt/nec/ve/" -DCMAKE_CXX_COMPILER="/opt/nec/nosupport/llvm-ve/bin/clang++" -DCMAKE_CXX_FLAGS="--target=ve-linux" -DLINKER_FLAGS="--target=ve-linux" ..
RUN make install -j12

WORKDIR ../../

RUN yum install -y which
