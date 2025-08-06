#include <iostream>
#include <string>
#include <unordered_map>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sstream>
#include "preload.hpp"

constexpr double MIN_INTERVAL = 2.0;  // 每个包名的限频时间，单位秒

std::vector<Chunk> pubg_chunks;

// 当前时间戳（秒）
double now_sec() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// 包名 → 预读文件列表的映射
std::unordered_map<std::string, std::string> preload_map = {
    {"com.tencent.ig",        "/data/local/tmp/PUBG_large.txt"},
    {"com.zhiliaoapp.musically", "TikTok_large.txt"},
    // 添加更多 app 映射
};

int main() {
    FILE* fp = popen("logcat -v brief ActivityTaskManager:I *:S", "r");
    if (!fp) {
        perror("popen logcat");
        return 1;
    }

    std::unordered_map<std::string, double> last_trigger_time;
    char line[1024];

    preload_prepare("PUBG_large.txt", pubg_chunks);
    std::cout<<pubg_chunks.size()<<std::endl;

    while (fgets(line, sizeof(line), fp)) {
        std::string str_line(line);

        // 找到 START u0 且匹配任意包名
        if (str_line.find("START u0") != std::string::npos) {
            for (const auto& [pkg, filelist] : preload_map) {
                if (str_line.find(pkg) != std::string::npos) {
                    double now = now_sec();
                    double& last = last_trigger_time[pkg];

                    if (now - last >= MIN_INTERVAL) {
                        std::cout << "[trigger] Launch of " << pkg << " → preload " << filelist << std::endl;
                        preload_execute(pubg_chunks);
                        last = now;
                    } else {
                        std::cout << "[skip] " << pkg << " triggered too soon (" << (now - last) << "s), skip\n";
                    }
                    break; // 匹配一个包名就跳出
                }
            }
        }
    }

    pclose(fp);
    return 0;
}
