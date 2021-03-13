#!/bin/bash
pushd ..
./sync_remote.sh
popd
ssh -t aurora "cd release/leveldb; ./build.sh '-DVECTOR_CRC32C -DVE_OPT'"
