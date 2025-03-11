# 运行脚本前先：sudo su
# 确保 ftrace 已挂载  ---- mount | grep tracefs
# 确保 nvme0n1 已挂载

#  清除 index 和 res
./clean_index_and_res.sh 

# 清除 DiskANN/trace
rm trace

#  清空跟踪缓冲区
echo > /sys/kernel/tracing/trace

# 使用 function_graph 跟踪器以显示函数调用关系
echo function_graph > /sys/kernel/tracing/current_tracer

# 将追踪函数添加到跟踪过滤器
echo blkdev_direct_IO > /sys/kernel/tracing/set_ftrace_filter

# 记录堆栈信息
echo 1 > /sys/kernel/tracing/options/func_stack_trace

# 设置单个 CPU 的缓冲区大小为 1024 KB（1 MB）,防止 trace 文件过大
echo 1024 > /sys/kernel/tracing/buffer_size_kb

# 启用跟踪
echo 1 > /sys/kernel/tracing/tracing_on

# 运行 DiskANN
cd build
sudo ./apps/build_disk_index \
    --data_type float --dist_fn l2 --data_path data/sift/sift_learn.fbin  \
    --index_path_prefix /mnt/ssd/disk_index_sift_learn_R32_L50_A1.2  \
    -R 32 -L50 -B 0.003 -M 1

# 停止跟踪
echo 0 > /sys/kernel/tracing/tracing_on

cd ..

# 查看追踪结果
cp /sys/kernel/tracing/trace ./