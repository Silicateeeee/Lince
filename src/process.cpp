#include "process.hpp"
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <algorithm>

std::vector<ProcessInfo> getRunningProcesses() {
    std::vector<ProcessInfo> processes;
    DIR* dir = opendir("/proc");
    if (!dir) return processes;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            std::string d_name = entry->d_name;
            if (std::all_of(d_name.begin(), d_name.end(), ::isdigit)) {
                pid_t pid = std::stoi(d_name);

                std::string name = "Unknown";
                std::ifstream comm_file("/proc/" + d_name + "/comm");
                if (comm_file.is_open()) {
                    std::getline(comm_file, name);
                }

                std::string cmdline = "";
                std::ifstream cmd_file("/proc/" + d_name + "/cmdline");
                if (cmd_file.is_open()) {
                    std::getline(cmd_file, cmdline, '\0');
                }

                processes.push_back({pid, name, cmdline});
            }
        }
    }
    closedir(dir);
    return processes;
}

bool processNameContains(const std::string& name, const std::string& filter) {
    if (filter.empty()) return true;
    std::string nameLower = name;
    std::string filterLower = filter;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
    return nameLower.find(filterLower) != std::string::npos;
}
