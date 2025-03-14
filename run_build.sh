# sudo su

./clean_index_and_res.sh

cd build

# echo 'file fs/aio.c line 1456 +p' > /sys/kernel/debug/dynamic_debug/control

sudo ./apps/build_disk_index \
    --data_type float --dist_fn l2 --data_path data/sift/sift_learn.fbin  \
    --index_path_prefix /mnt/ssd/disk_index_sift_learn_R32_L50_A1.2  \
    -R 32 -L50 -B 0.003 -M 1

# sudo echo 'file fs/aio.c =_' > /sys/kernel/debug/dynamic_debug/control

cd ..

sudo dmesg > log