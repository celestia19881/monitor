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
    {"com.booking",            "/data/local/tmp/log/Booking_large.txt"},
    {"com.adobe.psmobile",     "/data/local/tmp/log/Photoshop_large.txt"},
    {"com.twitter.android",    "/data/local/tmp/log/Twitter_large.txt"},
    {"com.tencent.ig",        "/data/local/tmp/log/PUBG_large.txt"},
    {"com.zhiliaoapp.musically", "/data/local/tmp/log/TikTok_large.txt"},
    {"com.xingin.xhs",         "/data/local/tmp/log/RedNote_large.txt"},
    {"com.lemon.lvoverseas",   "/data/local/tmp/log/Capcut_large.txt"},
    {"com.campmobile.snow",    "/data/local/tmp/log/Snow_large.txt"},
    {"com.google.earth",       "/data/local/tmp/log/GoogleEarth_large.txt"},
    {"com.roblox.client",      "/data/local/tmp/log/Roblox_large.txt"},
    {"com.tinder",             "/data/local/tmp/log/Tinder_large.txt"},
    {"com.einnovation.temu",   "/data/local/tmp/log/Temu_large.txt"},
    {"com.ubercab",            "/data/local/tmp/log/Uber_large.txt"},

    // 添加更多 app 映射
};

int main() {
    // 打开 logcat 进程
    FILE* fp = popen("logcat -v brief ActivityTaskManager:I *:S", "r");
    if (!fp) {
        perror("popen logcat");
        return 1;
    }

    // 创建一个无序映射来记录上一个触发时间
    std::unordered_map<std::string, double> last_trigger_time;
    char line[1024];

    // 加载预加载准备数据
    preload_prepare("PUBG_large.txt", pubg_chunks);
    std::cout << pubg_chunks.size() << std::endl;

    // 逐行读取 logcat 输出
    while (fgets(line, sizeof(line), fp)) {
        std::string str_line(line);

        // 找到 START u0 且匹配任意包名
        if (str_line.find("START u0") != std::string::npos) {
            // 遍历预加载映射
            for (const auto& [pkg, filelist] : preload_map) {
                // 如果找到匹配的包名
                if (str_line.find(pkg) != std::string::npos) {
                    double now = now_sec();
                    double& last = last_trigger_time[pkg];

                    // 如果当前时间与上次触发时间之差大于或等于最小间隔时间
                    if (now - last >= MIN_INTERVAL) {
                        std::cout << "[trigger] Launch of " << pkg << " → preload " << filelist << std::endl;
                        preload_execute(pubg_chunks);
                        last = now;
                    } else {
                        // 如果时间间隔小于最小间隔时间，则跳过
                        std::cout << "[skip] " << pkg << " triggered too soon (" << (now - last) << "s), skip\n";
                    }
                    break; // 匹配一个包名就跳出
                }
            }
        }
    }

    // 关闭 logcat 进程
    pclose(fp);
    return 0;
}
