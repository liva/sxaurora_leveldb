#!/bin/bash
#GDB="/opt/nec/ve/bin/gdb -x gdbcmd --args"

#if [ "${GDB}" == "" ]; then
#    OUTPUT=">> ./leveldb_result/${2}_$3"
#fi

function leveldb() {
    pushd $1
    # --num=1000
    sudo $GDB ./build/db_bench --write_buffer_size=65550000 --num=1000 --benchmarks="$2" --value_size=$3 $4 ${OUTPUT}
    popd
}

function bench_set_vepci() {
    #cleanup
    echo '0000:b3:00.0' | sudo tee /sys/bus/pci/drivers/uio_pci_generic/unbind
    echo '0000:b3:00.0' | sudo tee /sys/bus/pci/drivers/nvme/bind
    sleep 3
    
    sudo blkdiscard /dev/nvme0n1
    echo '0000:b3:00.0' | sudo tee /sys/bus/pci/drivers/nvme/unbind
    echo '0000:b3:00.0' | sudo tee /sys/bus/pci/drivers/uio_pci_generic/bind
    leveldb leveldb $1 $2 --vefs=1
    echo '0000:b3:00.0' | sudo tee /sys/bus/pci/drivers/uio_pci_generic/unbind
    echo '0000:b3:00.0' | sudo tee /sys/bus/pci/drivers/nvme/bind
    sleep 3
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

function benchmark() {
for vsize in 65540 #100 1024 8192 65536 65540
do
    for workload in fillseq,readrandom #fillseq,readseq fillseq,readrandom fillrandom fillsync
		#fillseq,readrandom fillseq fillrandom fillseq,readseq fillseq,readrandom fillseq,deleteseq fillseq,deleterandom fillseq,overwrite
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
#pushd leveldb
#./build.sh
#popd
#benchmark

pushd leveldb
./build.sh "-DVECTOR_CRC32C -DVE_OPT" #-DVECTOR_MEMTABLE"
popd
benchmark
