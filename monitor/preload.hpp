#pragma once
#include <string>
#include <vector>
#include <sys/types.h> // for off_t

struct Chunk {
    int fd;
    off_t off;
    size_t len;
};

bool preload_prepare(const std::string& list_path, std::vector<Chunk>& chunks);
void preload_execute(const std::vector<Chunk>& chunks);
