#include "preload.hpp"

#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <cstring>
#include <sys/syscall.h>

#ifndef O_NOATIME
#define O_NOATIME 01000000
#endif

#define BLOCK_SIZE (16 * 1024 * 1024)
#define THREADS 4


static inline double now_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static inline int try_readahead(int fd, off_t off, size_t len) {
#ifdef SYS_readahead
    return syscall(SYS_readahead, fd, (off64_t)off, (size_t)len);
#else
    return -1;
#endif
}

static size_t do_chunk_io(int fd, off_t off, size_t len, char* buf, size_t buf_sz) {
    if (try_readahead(fd, off, len) == 0) return len;

    size_t total = 0, left = len;
    while (left > 0) {
        size_t want = (left > buf_sz) ? buf_sz : left;
        ssize_t n = pread(fd, buf, want, off);
        if (n > 0) {
            off  += n;
            left -= n;
            total += n;
        } else if (n == 0) {
            break;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }
    return total;
}

bool preload_prepare(const std::string& list_path, std::vector<Chunk>& chunks_out) {
    std::ifstream in(list_path);
    if (!in) {
        std::cerr << "❌ Failed to open file list: " << list_path << "\n";
        return false;
    }

    std::string line;
    int total_lines = 0, opened_files = 0, skipped_files = 0;

    while (std::getline(in, line)) {
        ++total_lines;
        if (line.empty()) {
            std::cerr << "⚠️  Line " << total_lines << " is empty, skipping.\n";
            continue;
        }

        // 去除行尾换行
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        int fd = open(line.c_str(), O_RDONLY | O_CLOEXEC | O_NOATIME);
        if (fd == -1) {
            fd = open(line.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd == -1) {
                std::cerr << "❌ Failed to open (both NOATIME and fallback): " << line
                          << " | errno=" << errno << " (" << strerror(errno) << ")\n";
                ++skipped_files;
                continue;
            } else {
                std::cerr << "✅ Opened without O_NOATIME: " << line << "\n";
            }
        } else {
            std::cerr << "✅ Opened: " << line << "\n";
        }

        struct stat st;
        if (fstat(fd, &st) == -1) {
            std::cerr << "❌ fstat failed for: " << line
                      << " | errno=" << errno << " (" << strerror(errno) << ")\n";
            close(fd);
            ++skipped_files;
            continue;
        }

        if (st.st_size <= 0) {
            std::cerr << "⚠️  Skipping empty file: " << line << " (size = 0)\n";
            close(fd);
            ++skipped_files;
            continue;
        }

        // 提示顺序读取
        posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
        posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);

        off_t sz = st.st_size;
        size_t chunks_for_file = 0;
        for (off_t off = 0; off < sz; off += BLOCK_SIZE) {
            size_t len = (off + BLOCK_SIZE <= sz) ? BLOCK_SIZE : (sz - off);
            chunks_out.push_back({fd, off, len});
            ++chunks_for_file;
        }

        std::cerr << "📦 File added: " << line
                  << " | size = " << sz << " | chunks = " << chunks_for_file << "\n";
        ++opened_files;
    }

    std::cerr << "📊 Summary: " << total_lines << " lines, "
              << opened_files << " files loaded, "
              << skipped_files << " skipped.\n";

    std::cerr << "✅ Total chunks prepared: " << chunks_out.size() << "\n";
    return true;
}

struct WorkerCtx {
    const std::vector<Chunk>* chunks;
    std::mutex* lock;
    size_t next_idx = 0;
    size_t total_bytes = 0;
};

void* preload_worker(void* arg) {
    WorkerCtx* ctx = static_cast<WorkerCtx*>(arg);
    char* buf = nullptr;
    posix_memalign((void**)&buf, 4096, BLOCK_SIZE);

    size_t local_bytes = 0;

    while (true) {
        size_t i;
        {
            std::lock_guard<std::mutex> g(*ctx->lock);
            if (ctx->next_idx >= ctx->chunks->size()) break;
            i = ctx->next_idx++;
        }

        const Chunk& c = (*ctx->chunks)[i];
        local_bytes += do_chunk_io(c.fd, c.off, c.len, buf, BLOCK_SIZE);
    }

    free(buf);

    // 线程安全统计
    {
        std::lock_guard<std::mutex> g(*ctx->lock);
        ctx->total_bytes += local_bytes;
    }

    return nullptr;
}

void preload_execute(const std::vector<Chunk>& chunks) {
    if (chunks.empty()) {
        std::cerr << "No chunks to preload.\n";
        return;
    }

    double t0 = now_sec();

    WorkerCtx ctx{&chunks, new std::mutex(), 0, 0};

    pthread_t th[THREADS];
    for (int i = 0; i < THREADS; ++i) pthread_create(&th[i], nullptr, preload_worker, &ctx);
    for (int i = 0; i < THREADS; ++i) pthread_join(th[i], nullptr);

    double t1 = now_sec();

    double gb = (double)ctx.total_bytes / (1024.0 * 1024.0 * 1024.0);
    double sec = t1 - t0;
    double gbps = sec > 0 ? gb / sec : 0.0;

    std::cout << "Preloaded " << gb << " GiB in " << sec << " s ("
              << gbps << " GiB/s)\n";

    delete ctx.lock;
}
