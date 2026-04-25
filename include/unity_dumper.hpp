#ifndef UNITY_DUMPER_HPP
#define UNITY_DUMPER_HPP

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

namespace laugh {

struct UnityField {
    std::string name;
    std::string type;
    uintptr_t offset;
};

struct UnityMethod {
    std::string name;
    uintptr_t rva;
};

struct UnityClass {
    std::string name;
    std::map<std::string, UnityMethod> methods;
    std::map<std::string, UnityField> fields;
};

struct UnityNamespace {
    std::string name;
    std::map<std::string, UnityClass> classes;
};

class UnityDumper {
public:
    UnityDumper();
    bool loadDump(const std::string& path);
    
    bool isLoaded() const { return m_loaded; }
    bool isLoading() const { return m_isLoading; }
    float getLoadProgress() const { return m_loadProgress; }

    uintptr_t findMethodRVA(const std::string& ns, const std::string& className, const std::string& methodName);
    
    std::vector<std::string> searchClasses(const std::string& name);
    std::vector<std::string> listMethods(const std::string& ns, const std::string& className);
    std::vector<UnityField> listFields(const std::string& ns, const std::string& className);

    size_t getClassCount() const;

private:
    std::map<std::string, UnityNamespace> m_namespaces;
    std::atomic<bool> m_loaded{false};
    std::atomic<bool> m_isLoading{false};
    std::atomic<float> m_loadProgress{0.0f};
    mutable std::mutex m_mutex;
};

} // namespace laugh

#endif
