#!/bin/bash
pushd ..
./sync_remote.sh
popd
ssh -t aurora "cd release/leveldb; ./build.sh"
