#!/bin/bash
cd $(cd $(dirname $0);pwd)
../sync_remote.sh
ssh -t aurora "cd release/leveldb; ./build.sh 'VECTOR_CRC32C VE_OPT'"
