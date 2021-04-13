#!/bin/bash
cd $(cd $(dirname $0);pwd)
./sync_remote.sh
ssh -t aurora "cd release; ./leveldb_bench.sh"

