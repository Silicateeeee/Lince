#include "unity_dumper.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <algorithm>

namespace laugh {

UnityDumper::UnityDumper() : m_loaded(false), m_isLoading(false), m_loadProgress(0.0f) {}

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool UnityDumper::loadDump(const std::string& path) {
    if (m_isLoading) return false;

    m_isLoading = true;
    m_loaded = false;
    m_loadProgress = 0.0f;

    std::thread([this, path]() {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Failed to open dump file: " << path << std::endl;
            m_isLoading = false;
            return;
        }

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_namespaces.clear();

        std::string line;
        std::string currentNamespace = "";
        std::string currentClass = "";
        std::streamsize bytesRead = 0;

        while (std::getline(file, line)) {
            bytesRead += line.length() + 1;
            m_loadProgress = (float)bytesRead / (float)fileSize;

            std::string tline = trim(line);
            if (tline.empty()) continue;

            // 1. Check for Namespace
            size_t nsPos = tline.find("Namespace: ");
            if (nsPos != std::string::npos) {
                currentNamespace = trim(tline.substr(nsPos + 11));
                continue;
            }

            // 2. Check for RVA (Method Marker)
            size_t rvaPos = tline.find("RVA: 0x");
            if (rvaPos != std::string::npos) {
                std::string rvaStr = tline.substr(rvaPos + 7);
                // Extract only the hex part (until space)
                size_t spacePos = rvaStr.find(' ');
                if (spacePos != std::string::npos) rvaStr = rvaStr.substr(0, spacePos);
                
                uintptr_t rva = 0;
                try {
                    rva = std::stoull(rvaStr, nullptr, 16);
                } catch (...) {}

                // Method name is on the next meaningful line
                std::string methodLine;
                while (std::getline(file, methodLine)) {
                    bytesRead += methodLine.length() + 1;
                    std::string mtline = trim(methodLine);
                    if (mtline.empty() || mtline[0] == '[' || mtline.find("//") == 0) continue;
                    
                    size_t parenPos = mtline.find('(');
                    if (parenPos != std::string::npos) {
                        size_t nameStart = mtline.find_last_of(' ', parenPos - 1);
                        if (nameStart == std::string::npos) nameStart = 0;
                        else nameStart++;

                        std::string methodName = mtline.substr(nameStart, parenPos - nameStart);
                        if (!currentClass.empty() && rva != 0) {
                            m_namespaces[currentNamespace].classes[currentClass].methods[methodName] = {methodName, rva};
                        }
                    }
                    break;
                }
                continue;
            }

            // 3. Check for Class (Skip if it's a comment or within method)
            if (tline.find("//") != 0) {
                size_t classPos = tline.find("class ");
                if (classPos != std::string::npos) {
                    if (classPos == 0 || tline[classPos-1] == ' ') {
                        size_t nameStart = classPos + 6;
                        size_t nameEnd = tline.find_first_of(" :{\r\n", nameStart);
                        if (nameEnd == std::string::npos) nameEnd = tline.length();
                        currentClass = tline.substr(nameStart, nameEnd - nameStart);
                    }
                } else if (!currentClass.empty() && tline.find(';') != std::string::npos) {
                    // 4. Check for Fields
                    // Example: public float speed; // 0x10
                    size_t commentPos = tline.find("// 0x");
                    if (commentPos != std::string::npos) {
                        std::string offsetStr = tline.substr(commentPos + 5);
                        uintptr_t offset = 0;
                        try { offset = std::stoull(offsetStr, nullptr, 16); } catch(...) {}
                        
                        if (offset != 0) {
                            std::string decl = tline.substr(0, tline.find(';'));
                            size_t nameEnd = decl.find_last_not_of(" ");
                            size_t nameStart = decl.find_last_of(" ", nameEnd);
                            if (nameStart == std::string::npos) nameStart = 0; else nameStart++;
                            
                            std::string fieldName = decl.substr(nameStart, nameEnd - nameStart + 1);
                            
                            std::string typePart = decl.substr(0, nameStart);
                            std::string fieldType = trim(typePart);
                            // Strip modifiers like public/private
                            size_t lastMod = fieldType.find_last_of(' ');
                            if (lastMod != std::string::npos) fieldType = fieldType.substr(lastMod + 1);

                            m_namespaces[currentNamespace].classes[currentClass].fields[fieldName] = {fieldName, fieldType, offset};
                        }
                    }
                }
            }
        }

        m_loaded = true;
        m_isLoading = false;
        m_loadProgress = 1.0f;
    }).detach();

    return true;
}

uintptr_t UnityDumper::findMethodRVA(const std::string& ns, const std::string& className, const std::string& methodName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto nsIt = m_namespaces.find(ns);
    if (nsIt == m_namespaces.end()) return 0;

    auto classIt = nsIt->second.classes.find(className);
    if (classIt == nsIt->second.classes.end()) return 0;

    auto methodIt = classIt->second.methods.find(methodName);
    if (methodIt == classIt->second.methods.end()) return 0;

    return methodIt->second.rva;
}

std::vector<std::string> UnityDumper::searchClasses(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    for (auto const& [ns, n] : m_namespaces) {
        for (auto const& [className, c] : n.classes) {
            if (className.find(name) != std::string::npos) {
                results.push_back(ns + (ns.empty() ? "" : ".") + className);
            }
        }
    }
    return results;
}

std::vector<std::string> UnityDumper::listMethods(const std::string& ns, const std::string& className) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    auto nsIt = m_namespaces.find(ns);
    if (nsIt == m_namespaces.end()) return results;

    auto classIt = nsIt->second.classes.find(className);
    if (classIt == nsIt->second.classes.end()) return results;

    for (auto const& [methodName, m] : classIt->second.methods) {
        results.push_back(methodName);
    }
    return results;
}

std::vector<UnityField> UnityDumper::listFields(const std::string& ns, const std::string& className) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<UnityField> results;
    auto nsIt = m_namespaces.find(ns);
    if (nsIt == m_namespaces.end()) return results;

    auto classIt = nsIt->second.classes.find(className);
    if (classIt == nsIt->second.classes.end()) return results;

    for (auto const& [fieldName, f] : classIt->second.fields) {
        results.push_back(f);
    }
    return results;
}

size_t UnityDumper::getClassCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (auto const& [ns, n] : m_namespaces) {
        count += n.classes.size();
    }
    return count;
}

} // namespace laugh
