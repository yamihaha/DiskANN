// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include <libaio.h>
#include <cassert>
#include <fcntl.h>     // 包含 open 函数的声明
#include <unistd.h>    // 包含 close 函数的声明

#include "logger.h"
#include "ann_exception.h"

// sequential cached reads
class cached_ifstream
{
  public:
    cached_ifstream()
    {
    }
    cached_ifstream(const std::string &filename, uint64_t cacheSize) : cache_size(cacheSize), cur_off(0)
    {
        reader.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        this->open(filename, cache_size);
    }
    ~cached_ifstream()
    {
        delete[] cache_buf;
        reader.close();
    }

    void open(const std::string &filename, uint64_t cacheSize)
    {
        this->cur_off = 0;

        try
        {
            reader.open(filename, std::ios::binary | std::ios::ate);
            fsize = reader.tellg();
            reader.seekg(0, std::ios::beg);
            assert(reader.is_open());
            assert(cacheSize > 0);
            cacheSize = (std::min)(cacheSize, fsize);
            this->cache_size = cacheSize;
            cache_buf = new char[cacheSize];
            reader.read(cache_buf, cacheSize);
            diskann::cout << "Opened: " << filename.c_str() << ", size: " << fsize << ", cache_size: " << cacheSize
                          << std::endl;
        }
        catch (std::system_error &e)
        {
            throw diskann::FileException(filename, e, __FUNCSIG__, __FILE__, __LINE__);
        }
    }

    size_t get_file_size()
    {
        return fsize;
    }

    void read(char *read_buf, uint64_t n_bytes)
    {
        assert(cache_buf != nullptr);
        assert(read_buf != nullptr);

        if (n_bytes <= (cache_size - cur_off))
        {
            // case 1: cache contains all data
            memcpy(read_buf, cache_buf + cur_off, n_bytes);
            cur_off += n_bytes;
        }
        else
        {
            // case 2: cache contains some data
            uint64_t cached_bytes = cache_size - cur_off;
            if (n_bytes - cached_bytes > fsize - reader.tellg())
            {
                std::stringstream stream;
                stream << "Reading beyond end of file" << std::endl;
                stream << "n_bytes: " << n_bytes << " cached_bytes: " << cached_bytes << " fsize: " << fsize
                       << " current pos:" << reader.tellg() << std::endl;
                diskann::cout << stream.str() << std::endl;
                throw diskann::ANNException(stream.str(), -1, __FUNCSIG__, __FILE__, __LINE__);
            }
            memcpy(read_buf, cache_buf + cur_off, cached_bytes);

            // go to disk and fetch more data
            reader.read(read_buf + cached_bytes, n_bytes - cached_bytes);
            // reset cur off
            cur_off = cache_size;

            uint64_t size_left = fsize - reader.tellg();

            if (size_left >= cache_size)
            {
                reader.read(cache_buf, cache_size);
                cur_off = 0;
            }
            // note that if size_left < cache_size, then cur_off = cache_size,
            // so subsequent reads will all be directly from file
        }
    }

  private:
    // underlying ifstream
    std::ifstream reader;
    // # bytes to cache in one shot read
    uint64_t cache_size = 0;
    // underlying buf for cache
    char *cache_buf = nullptr;
    // offset into cache_buf for cur_pos
    uint64_t cur_off = 0;
    // file size
    uint64_t fsize = 0;
};

// sequential cached writes
class cached_ofstream
{
  public:
    cached_ofstream(const std::string &filename, uint64_t cache_size) : cache_size(cache_size), cur_off(0)
    {
        writer.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try
        {
            writer.open(filename, std::ios::binary);      // 写入调用的是 ofstream.write
            assert(writer.is_open());
            assert(cache_size > 0);
            cache_buf = new char[cache_size];
            diskann::cout << "Opened: " << filename.c_str() << ", cache_size: " << cache_size << std::endl;
        }
        catch (std::system_error &e)
        {
            throw diskann::FileException(filename, e, __FUNCSIG__, __FILE__, __LINE__);
        }
    }

    ~cached_ofstream()
    {
        this->close();
    }

    void close()
    {
        // dump any remaining data in memory
        if (cur_off > 0)
        {
            this->flush_cache();
        }

        if (cache_buf != nullptr)
        {
            delete[] cache_buf;
            cache_buf = nullptr;
        }

        if (writer.is_open())
            writer.close();
        diskann::cout << "Finished writing " << fsize << "B" << std::endl;
    }

    size_t get_file_size()
    {
        return fsize;
    }
    // writes n_bytes from write_buf to the underlying ofstream/cache
    void write(char *write_buf, uint64_t n_bytes)
    {
        assert(cache_buf != nullptr);
        if (n_bytes <= (cache_size - cur_off))        // cache_size : 64*1024*1024 , 64 MB
        {
            // case 1: cache can take all data
            memcpy(cache_buf + cur_off, write_buf, n_bytes);
            cur_off += n_bytes;
        }
        else
        {
            // case 2: cache can not take all data
            // go to disk and write existing cache data
            writer.write(cache_buf, cur_off);
            fsize += cur_off;
            // write the new data to disk
            writer.write(write_buf, n_bytes);     // 实际写入过程，调用 ofstream.write 函数
            fsize += n_bytes;
            // memset all cache data and reset cur_off
            memset(cache_buf, 0, cache_size);
            cur_off = 0;
        }
    }

    void flush_cache()
    {
        assert(cache_buf != nullptr);
        writer.write(cache_buf, cur_off);
        fsize += cur_off;
        memset(cache_buf, 0, cache_size);
        cur_off = 0;
    }

    void reset()
    {
        flush_cache();
        writer.seekp(0);
    }

  private:
    // underlying ofstream
    std::ofstream writer;
    // # bytes to cache for one shot write
    uint64_t cache_size = 0;
    // underlying buf for cache
    char *cache_buf = nullptr;
    // offset into cache_buf for cur_pos
    uint64_t cur_off = 0;

    // file size
    uint64_t fsize = 0;
};

class CachedAioDirectWriter {
public:
    CachedAioDirectWriter(const std::string& filename, uint64_t cache_size, uint64_t num_buffers)
        : cache_size(cache_size), num_buffers(num_buffers), cur_off(0), file_offset(0), ctx(0) {
        // 初始化 AIO 上下文
        int ret = io_setup(128, &ctx);
        if (ret != 0) {
            throw std::runtime_error("io_setup failed: " + std::string(strerror(-ret)));
        }

        // 打开文件
        open(filename);

        // 分配缓存缓冲区
        for (uint64_t i = 0; i < num_buffers; ++i) {
            char* buf;
            ret = posix_memalign(reinterpret_cast<void**>(&buf), 512, cache_size);
            if (ret != 0) {
                throw std::runtime_error("posix_memalign failed: " + std::string(strerror(ret)));
            }
            cache_buffers.push_back(buf);
            pending_requests.push_back({});
        }
    }

    ~CachedAioDirectWriter() {
        try {
            flush(); // 刷新所有未完成的请求
        } catch (const std::exception& e) {
            std::cerr << "Error in ~CachedAioDirectWriter: " << e.what() << std::endl;
        }

        // 释放缓存缓冲区
        for (auto buf : cache_buffers) {
            free(buf);
        }

        // 销毁 AIO 上下文
        if (ctx != 0) {
            io_destroy(ctx);
        }

        // 关闭文件
        if (fd != -1) {
            close();
        }
    }

    void open(const std::string& filename) {
        fd = ::open(filename.c_str(), O_WRONLY | O_DIRECT | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            throw std::runtime_error("Failed to open file: " + std::string(strerror(errno)));
        }
    }

    void close() {
        flush(); // 刷新所有未完成的请求
        if (fd != -1) {
            ::close(fd);
            fd = -1;
        }
    }

    void write(const char* data, uint64_t size) {
        while (size > 0) {
            // 检查当前 cached_buf 是否已满
            if (cur_off == cache_size) {
                submit_current_buffer();
            }

            // 计算当前 cached_buf 剩余空间
            uint64_t remaining = cache_size - cur_off;
            uint64_t to_copy = std::min(remaining, size);

            // 将数据复制到当前 cached_buf
            memcpy(cache_buffers[current_buffer] + cur_off, data, to_copy);
            cur_off += to_copy;
            data += to_copy;
            size -= to_copy;
        }
    }

    void flush() {
        // 提交当前缓冲区的数据
        if (cur_off > 0) {
            submit_current_buffer();
        }

        // 等待所有未完成的请求完成
        wait_all();
    }

private:
    int fd = -1; // 文件描述符
    io_context_t ctx; // AIO 上下文
    uint64_t cache_size; // 每个缓存缓冲区的大小
    uint64_t num_buffers; // 缓存缓冲区的数量
    std::vector<char*> cache_buffers; // 缓存缓冲区列表
    uint64_t cur_off; // 当前缓存缓冲区的偏移量
    uint64_t file_offset; // 文件的写入偏移量
    uint64_t current_buffer = 0; // 当前使用的缓存缓冲区编号
    std::vector<std::vector<struct iocb>> pending_requests; // 每个缓存缓冲区的未完成请求

    // 提交当前缓存缓冲区的数据
    void submit_current_buffer() {
        if (cur_off == 0) {
            return; // 没有数据需要提交
        }

        // 检查当前缓存缓冲区是否有未完成的请求
        if (!pending_requests[current_buffer].empty()) {
            wait_for_buffer(current_buffer);
        }

        // 准备写请求
        struct iocb cb;
        io_prep_pwrite(&cb, fd, cache_buffers[current_buffer], cur_off, file_offset);

        // 提交写请求
        struct iocb* cbs[] = {&cb};
        int ret = io_submit(ctx, 1, cbs);
        if (ret != 1) {
            throw std::runtime_error("io_submit failed: " + std::string(strerror(-ret)));
        }

        // 更新文件偏移量
        file_offset += cur_off;

        // 记录未完成的请求
        pending_requests[current_buffer].push_back(cb);

        // 切换到下一个缓存缓冲区
        current_buffer = (current_buffer + 1) % num_buffers;
        cur_off = 0;
    }

    // 等待指定缓存缓冲区的所有请求完成
    void wait_for_buffer(uint64_t buffer_index) {
        auto& requests = pending_requests[buffer_index];
        if (requests.empty()) {
            return;
        }

        std::vector<struct io_event> events(requests.size());
        int ret = io_getevents(ctx, requests.size(), requests.size(), events.data(), nullptr);
        if (ret != static_cast<int>(requests.size())) {
            throw std::runtime_error("io_getevents failed: " + std::string(strerror(-ret)));
        }

        requests.clear();
    }

    // 等待所有未完成的请求完成
    void wait_all() {
        for (uint64_t i = 0; i < num_buffers; ++i) {
            wait_for_buffer(i);
        }
    }
};