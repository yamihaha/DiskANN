cd build

sudo ./apps/build_disk_index \
    --data_type float --dist_fn l2 --data_path data/sift/sift_learn.fbin  \
    --index_path_prefix /mnt/ssd/disk_index_sift_learn_R32_L50_A1.2  \
    -R 32 -L50 -B 0.003 -M 1

sudo  ./apps/search_disk_index   \
    --data_type float --dist_fn l2  \
    --index_path_prefix /mnt/ssd/disk_index_sift_learn_R32_L50_A1.2  \
    --query_file data/sift/sift_query.fbin   \
    --gt_file data/sift/sift_query_learn_gt100  \
    -K 10 -L 10 20 30 40 50 100  \
    --result_path data/sift/res  \
    --num_nodes_to_cache 10000