#!/bin/bash -le
#GDB="/opt/nec/ve/bin/gdb -x gdbcmd --args"

#if [ "${GDB}" == "" ]; then
#    OUTPUT=">> ./leveldb_result/${2}_$3"
#fi
SCRIPT_DIR="../scripts"

function leveldb() {
    local NUM=50000 #100000
    sudo $GDB ./build/db_bench --num="${NUM}" --benchmarks="$2" --value_size=$3 $4 ${OUTPUT}
}

function bench_set_vepci() {
    ${SCRIPT_DIR}/cleanup_nvme.sh
    ${SCRIPT_DIR}/setup_uio_nvme.sh
    leveldb leveldb $1 $2 --vefs=1
    ${SCRIPT_DIR}/cleanup_uio_nvme.sh
}

function bench_set_ramdisk() {
    sleep 3
    sudo mount -t tmpfs -o size=512m tmpfs /mnt/ramfs/
    leveldb $1 $2 $3 --db=/mnt/tmpfs/rocksdb
    sudo umount /mnt/ramfs
}

function bench_set_ssd() {
    sudo mkfs.ext4 /dev/nvme0n1 > /dev/null
    sudo mount /dev/nvme0n1 /mnt/ext4ssd/
    leveldb $1 $2 $3 --db=/mnt/ext4ssd/rocksdb
#    ls -lah /mnt/ext4ssd/rocksdb/
    sudo umount /mnt/ext4ssd
}

function bench_set_unvmemem() {
  	rm -rf /home/sawamoto/ssd_mem
  	dd if=/dev/zero of=/home/sawamoto/ssd_mem bs=1G count=10
    leveldb leveldb $1 $2 --vefs=1
  	rm -rf /home/sawamoto/ssd_mem
}

function benchmark() {
for workload in fillseq,readseq #fillseq,readrandom fillrandom #fillseq,readseq fillseq,readrandom fillrandom fillsync #fillseq,readrandom fillseq fillrandom fillseq,readseq fillseq,readrandom fillseq,deleteseq fillseq,deleterandom fillseq,overwrite
do
    for vsize in 204800 #100 #100 1024 8192 65536
    do
	#bench_set_ssd leveldb_x86 $workload $vsize
	#leveldb leveldb_x86 $workload $vsize --mem=1
	echo ""
#	echo redirect
#	bench_set_ssd leveldb $workload $vsize
#	echo memory
#	leveldb leveldb $workload $vsize --mem=1
#	echo redirect_ram
#	bench_set_ramdisk leveldb $workload $vsize
	echo unvme
	bench_set_vepci $workload $vsize
    done
done
}

date > output
#./build.sh
#benchmark

./build.sh "VECTOR_CRC32C VE_OPT"
benchmark
