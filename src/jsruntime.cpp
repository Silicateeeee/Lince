#include "jsruntime.hpp"
#include "mem_scanner.hpp"
#include "process.hpp"
#include "quickjs.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <thread>

namespace laugh {

std::atomic<int> JavaScriptEngine::s_nextId(0);
JavaScriptEngine* JavaScriptEngine::s_current = nullptr;

JavaScriptEngine::JavaScriptEngine() : m_id(++s_nextId) {
}

JavaScriptEngine::~JavaScriptEngine() {
    if (m_ctx) {
        JS_FreeContext(m_ctx);
    }
    if (m_rt) {
        JS_FreeRuntime(m_rt);
    }
}

bool JavaScriptEngine::init() {
    m_rt = JS_NewRuntime();
    if (!m_rt) {
        m_lastError = "Failed to create QuickJS runtime";
        return false;
    }

    m_ctx = JS_NewContext(m_rt);
    if (!m_ctx) {
        m_lastError = "Failed to create QuickJS context";
        return false;
    }

    s_current = this;
    m_valid = true;

    setupBindings();
    return true;
}

void JavaScriptEngine::setMemoryScanner(void* scanner) {
    m_memoryScanner = scanner;
}

void JavaScriptEngine::setProcessList(void* processes) {
    m_processList = processes;
}

void JavaScriptEngine::setAttachedProcess(int pid, const std::string& name) {
    m_attachedPid = pid;
    m_attachedName = name;
}

void JavaScriptEngine::addLog(ScriptLog::Level level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    
    m_logs.push_back({level, message, ss.str()});
    if (m_logs.size() > 1000) {
        m_logs.erase(m_logs.begin());
    }
}

static uint64_t toUint64(JSContext* ctx, JSValueConst val) {
    uint64_t v = 0;
    if (JS_IsBigInt(ctx, val)) {
        int64_t v64 = 0;
        JS_ToBigInt64(ctx, &v64, val);
        v = (uint64_t)v64;
    } else {
        int64_t v64 = 0;
        JS_ToInt64(ctx, &v64, val);
        v = (uint64_t)v64;
    }
    return v;
}

static int32_t toInt32(JSContext* ctx, JSValueConst val) {
    int32_t v = 0;
    JS_ToInt32(ctx, &v, val);
    return v;
}

static int64_t toInt64(JSContext* ctx, JSValueConst val) {
    int64_t v = 0;
    JS_ToInt64(ctx, &v, val);
    return v;
}

static double toFloat64(JSContext* ctx, JSValueConst val) {
    double v = 0;
    JS_ToFloat64(ctx, &v, val);
    return v;
}

static bool toBool(JSContext* ctx, JSValueConst val) {
    return JS_ToBool(ctx, val) != 0;
}

void JavaScriptEngine::setupBindings() {
    if (!m_ctx) return;

    JSValue global = JS_GetGlobalObject(m_ctx);

    JSValue memoryObj = JS_NewObject(m_ctx);
    JS_SetPropertyStr(m_ctx, global, "memory", memoryObj);

    JS_DefinePropertyValueStr(m_ctx, memoryObj, "read",
        JS_NewCFunction(m_ctx, jsReadMemory, "read", 2), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "write",
        JS_NewCFunction(m_ctx, jsWriteMemory, "write", 3), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "scan",
        JS_NewCFunction(m_ctx, jsScanMemory, "scan", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "AOB",
        JS_NewCFunction(m_ctx, jsAOBScan, "AOB", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "call",
        JS_NewCFunction(m_ctx, jsMemoryCall, "call", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "isScanning",
        JS_NewCFunction(m_ctx, jsIsScanning, "isScanning", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "getProgress",
        JS_NewCFunction(m_ctx, jsGetProgress, "getProgress", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "getResults",
        JS_NewCFunction(m_ctx, jsGetResults, "getResults", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "getModules",
        JS_NewCFunction(m_ctx, jsGetModules, "getModules", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "getProcessInfo",
        JS_NewCFunction(m_ctx, jsGetProcessInfo, "getProcessInfo", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);

    JSValue unityObj = JS_NewObject(m_ctx);
    JS_SetPropertyStr(m_ctx, global, "unity", unityObj);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "load",
        JS_NewCFunction(m_ctx, jsUnityLoad, "load", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "getAddress",
        JS_NewCFunction(m_ctx, jsUnityGetAddress, "getAddress", 3), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "setModuleName",
        JS_NewCFunction(m_ctx, jsUnitySetModuleName, "setModuleName", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "getModuleBase",
        JS_NewCFunction(m_ctx, jsUnityGetModuleBase, "getModuleBase", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "searchClasses",
        JS_NewCFunction(m_ctx, jsUnitySearchClasses, "searchClasses", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "listMethods",
        JS_NewCFunction(m_ctx, jsUnityListMethods, "listMethods", 2), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "getFields",
        JS_NewCFunction(m_ctx, jsUnityGetFields, "getFields", 2), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "findObject",
        JS_NewCFunction(m_ctx, jsUnityFindObject, "findObject", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "getComponents",
        JS_NewCFunction(m_ctx, jsUnityGetComponents, "getComponents", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "isLoading",
        JS_NewCFunction(m_ctx, jsUnityIsLoading, "isLoading", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "getLoadProgress",
        JS_NewCFunction(m_ctx, jsUnityGetLoadProgress, "getLoadProgress", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, unityObj, "isLoaded",
        JS_NewCFunction(m_ctx, jsUnityIsLoaded, "isLoaded", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);

    JSValue guiObj = JS_NewObject(m_ctx);
    JS_SetPropertyStr(m_ctx, global, "gui", guiObj);

    struct GUIBinding { const char* name; JSCFunction* func; int argc; } guiFuncs[] = {
        {"beginWindow", jsWindowBegin, 1}, {"endWindow", jsWindowEnd, 0},
        {"button", jsButton, 1}, {"text", jsText, 1},
        {"inputText", jsInputText, 2},
        {"inputInt", jsInputInt, 2}, {"inputFloat", jsInputFloat, 2},
        {"checkbox", jsCheckbox, 2}, {"sliderFloat", jsSliderFloat, 4},
        {"separator", jsSeparator, 0}, {"sameLine", jsSameLine, 0},
        {"beginChild", jsBeginChild, 1}, {"endChild", jsEndChild, 0},
        {"progressBar", jsProgressBar, 1}, {"combo", jsCombo, 3},
        {"treeNode", jsTreeNode, 1}, {"treePop", jsTreePop, 0},
        {"getScreenSize", jsGetScreenSize, 0}, {"getFrameCount", jsGetFrameCount, 0},
        {"getDeltaTime", jsGetDeltaTime, 0}, {"isKeyPressed", jsIsKeyPressed, 1},
        {"isMouseClicked", jsIsMouseClicked, 1}, {"getMousePos", jsGetMousePos, 0},
        {"drawLine", jsDrawLine, 4}, {"drawRect", jsDrawRect, 4},
        {"drawCircle", jsDrawCircle, 3}, {"drawText", jsDrawText, 3},
    };

    for (const auto& f : guiFuncs) {
        JS_DefinePropertyValueStr(m_ctx, guiObj, f.name,
            JS_NewCFunction(m_ctx, f.func, f.name, f.argc), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    }

    JS_DefinePropertyValueStr(m_ctx, global, "log",
        JS_NewCFunction(m_ctx, jsLog, "log", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);

    JS_FreeValue(m_ctx, global);

    const char* initCode = R"(
        var lastResult = [];
        var savedValues = new Map();
    )";
    JS_Eval(m_ctx, initCode, strlen(initCode), "<init>", JS_EVAL_TYPE_GLOBAL);
}

JSValue JavaScriptEngine::jsReadMemory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) return JS_NULL;
    if (argc < 2) return JS_NULL;

    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    uintptr_t addr = toUint64(ctx, argv[0]);
    int type = toInt32(ctx, argv[1]);

    switch (type) {
        case 0: return JS_NewUint32(ctx, ms->readMemory<uint8_t>(addr));
        case 1: return JS_NewUint32(ctx, ms->readMemory<uint16_t>(addr));
        case 2: return JS_NewUint32(ctx, ms->readMemory<uint32_t>(addr));
        case 3: return JS_NewBigUint64(ctx, ms->readMemory<uint64_t>(addr));
        case 4: return JS_NewFloat64(ctx, ms->readMemory<float>(addr));
        case 5: return JS_NewFloat64(ctx, ms->readMemory<double>(addr));
        case 6: {
            std::string str = ms->readString(addr, 64);
            return JS_NewString(ctx, str.c_str());
        }
        default: return JS_NULL;
    }
}

JSValue JavaScriptEngine::jsWriteMemory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) return JS_FALSE;
    if (argc < 3) return JS_FALSE;

    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    uintptr_t addr = toUint64(ctx, argv[0]);
    int type = toInt32(ctx, argv[2]);

    switch (type) {
        case 0: return ms->writeMemory<uint8_t>(addr, (uint8_t)toInt32(ctx, argv[1])) ? JS_TRUE : JS_FALSE;
        case 1: return ms->writeMemory<uint16_t>(addr, (uint16_t)toInt32(ctx, argv[1])) ? JS_TRUE : JS_FALSE;
        case 2: return ms->writeMemory<uint32_t>(addr, (uint32_t)toInt32(ctx, argv[1])) ? JS_TRUE : JS_FALSE;
        case 3: return ms->writeMemory<uint64_t>(addr, (uint64_t)toInt64(ctx, argv[1])) ? JS_TRUE : JS_FALSE;
        case 4: return ms->writeMemory<float>(addr, (float)toFloat64(ctx, argv[1])) ? JS_TRUE : JS_FALSE;
        case 5: return ms->writeMemory<double>(addr, (double)toFloat64(ctx, argv[1])) ? JS_TRUE : JS_FALSE;
        case 7: { // AOB Patch
            const char* pattern = JS_ToCString(ctx, argv[1]);
            bool success = false;
            if (pattern) {
                success = ms->patch(addr, pattern);
                JS_FreeCString(ctx, pattern);
            }
            return success ? JS_TRUE : JS_FALSE;
        }
        default: return JS_FALSE;
    }
}

JSValue JavaScriptEngine::jsScanMemory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) return JS_NewArray(ctx);
    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    auto results = ms->getResults();
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < results.size(); ++i) {
        JS_SetPropertyUint32(ctx, arr, i, JS_NewBigUint64(ctx, results[i].address));
    }
    return arr;
}

JSValue JavaScriptEngine::jsAOBScan(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner || argc < 1) return JS_NULL;
    const char* pattern = JS_ToCString(ctx, argv[0]);
    if (!pattern) return JS_NULL;
    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    if (ms->isScanning()) {
        JS_FreeCString(ctx, pattern);
        return JS_ThrowInternalError(ctx, "Scan already in progress");
    }
    ms->firstScan(ValueType::AOB, pattern);
    JS_FreeCString(ctx, pattern);
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    s_current->m_pendingPromises.push_back({JS_DupValue(ctx, resolving_funcs[0]), JS_DupValue(ctx, resolving_funcs[1])});
    return promise;
}

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

JSValue JavaScriptEngine::jsMemoryCall(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner || argc < 1) return JS_FALSE;
    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    uint64_t targetAddr = toUint64(ctx, argv[0]);
    pid_t pid = (pid_t)s_current->m_attachedPid;

    if (pid <= 0) return JS_ThrowInternalError(ctx, "No process attached");

    // Hijack via ptrace (Requires sudo)
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
        return JS_ThrowInternalError(ctx, "Failed to attach (PTRACE_ATTACH). Run with sudo.");
    }
    
    waitpid(pid, NULL, 0);

    struct user_regs_struct oldregs, regs;
    ptrace(PTRACE_GETREGS, pid, NULL, &oldregs);
    memcpy(&regs, &oldregs, sizeof(struct user_regs_struct));

    // Simple x64 calling convention (RDI, RSI, RDX, RCX, R8, R9)
    if (argc > 1) regs.rdi = toUint64(ctx, argv[1]);
    if (argc > 2) regs.rsi = toUint64(ctx, argv[2]);
    if (argc > 3) regs.rdx = toUint64(ctx, argv[3]);

    // Push original RIP as return address (unsafe, but basic stub)
    // For a real call we should use a codecave with an INT3 or loop
    regs.rip = targetAddr;
    
    ptrace(PTRACE_SETREGS, pid, NULL, &regs);
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    
    // In a real implementation we would wait for a breakpoint or signal
    // For now, we immediately detach (this is very risky/limited)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ptrace(PTRACE_DETACH, pid, NULL, NULL);

    return JS_TRUE;
}

JSValue JavaScriptEngine::jsIsScanning(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) return JS_FALSE;
    return static_cast<MemScanner*>(s_current->m_memoryScanner)->isScanning() ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsGetProgress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) return JS_NewFloat64(ctx, 0.0);
    return JS_NewFloat64(ctx, static_cast<MemScanner*>(s_current->m_memoryScanner)->getProgress());
}

JSValue JavaScriptEngine::jsGetResults(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) return JS_NewArray(ctx);
    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    auto results = ms->getResults();
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < results.size(); ++i) {
        JS_SetPropertyUint32(ctx, arr, i, JS_NewBigUint64(ctx, results[i].address));
    }
    return arr;
}

JSValue JavaScriptEngine::jsGetModules(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) return JS_NewArray(ctx);
    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    auto regions = ms->getRegions();
    JSValue arr = JS_NewArray(ctx);
    uint32_t idx = 0;
    std::string lastPath = "";
    for (const auto& reg : regions) {
        if (!reg.pathname.empty() && reg.pathname != lastPath) {
            JSValue obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, reg.pathname.c_str()));
            JS_SetPropertyStr(ctx, obj, "base", JS_NewBigUint64(ctx, reg.start));
            JS_SetPropertyUint32(ctx, arr, idx++, obj);
            lastPath = reg.pathname;
        }
    }
    return arr;
}

JSValue JavaScriptEngine::jsGetProcessInfo(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current) return JS_NULL;
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "pid", JS_NewInt32(ctx, s_current->m_attachedPid));
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, s_current->m_attachedName.c_str()));
    return obj;
}

JSValue JavaScriptEngine::jsWindowBegin(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    const char* name = JS_ToCString(ctx, argv[0]);
    bool result = ImGui::Begin(name, nullptr, 0);
    JS_FreeCString(ctx, name);
    return result ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsWindowEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ImGui::End();
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsButton(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    const char* label = JS_ToCString(ctx, argv[0]);
    bool result = ImGui::Button(label);
    JS_FreeCString(ctx, label);
    return result ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsText(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_TRUE;
    const char* text = JS_ToCString(ctx, argv[0]);
    ImGui::Text("%s", text ? text : "");
    if (text) JS_FreeCString(ctx, text);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsInputText(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;
    const char* label = JS_ToCString(ctx, argv[0]);
    const char* value = JS_ToCString(ctx, argv[1]);
    
    char buffer[1024];
    strncpy(buffer, value ? value : "", sizeof(buffer)-1);
    
    if (ImGui::InputText(label, buffer, sizeof(buffer))) {
        JS_FreeCString(ctx, label);
        JS_FreeCString(ctx, value);
        return JS_NewString(ctx, buffer);
    }
    
    JS_FreeCString(ctx, label);
    JS_FreeCString(ctx, value);
    return JS_NewString(ctx, buffer);
}

JSValue JavaScriptEngine::jsInputInt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_NewInt32(ctx, 0);
    const char* label = JS_ToCString(ctx, argv[0]);
    int value = toInt32(ctx, argv[1]);
    ImGui::InputInt(label, &value);
    JS_FreeCString(ctx, label);
    return JS_NewInt32(ctx, value);
}

JSValue JavaScriptEngine::jsInputFloat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_NewFloat64(ctx, 0.0);
    const char* label = JS_ToCString(ctx, argv[0]);
    double value = toFloat64(ctx, argv[1]);
    float fval = (float)value;
    ImGui::InputFloat(label, &fval);
    JS_FreeCString(ctx, label);
    return JS_NewFloat64(ctx, fval);
}

JSValue JavaScriptEngine::jsCheckbox(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_FALSE;
    const char* label = JS_ToCString(ctx, argv[0]);
    bool value = toBool(ctx, argv[1]);
    ImGui::Checkbox(label, &value);
    JS_FreeCString(ctx, label);
    return value ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsSliderFloat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 4) return JS_NewFloat64(ctx, 0.0);
    const char* label = JS_ToCString(ctx, argv[0]);
    float value = (float)toFloat64(ctx, argv[1]);
    float vmin = (float)toFloat64(ctx, argv[2]);
    float vmax = (float)toFloat64(ctx, argv[3]);
    ImGui::SliderFloat(label, &value, vmin, vmax);
    JS_FreeCString(ctx, label);
    return JS_NewFloat64(ctx, value);
}

JSValue JavaScriptEngine::jsSeparator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ImGui::Separator();
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsSameLine(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    float offset = 0.0f;
    float spacing = -1.0f;
    if (argc > 0) offset = (float)toFloat64(ctx, argv[0]);
    if (argc > 1) spacing = (float)toFloat64(ctx, argv[1]);
    ImGui::SameLine(offset, spacing);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsBeginChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const char* id = "";
    if (argc > 0) id = JS_ToCString(ctx, argv[0]);
    ImVec2 size(0, 0);
    if (argc > 1) {
        double a = toFloat64(ctx, argv[1]);
        double b = argc > 2 ? toFloat64(ctx, argv[2]) : 0;
        size = ImVec2((float)a, (float)b);
    }
    bool result = ImGui::BeginChild(id, size, true);
    if (id[0]) JS_FreeCString(ctx, id);
    return result ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsEndChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ImGui::EndChild();
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsProgressBar(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_TRUE;
    float fraction = (float)toFloat64(ctx, argv[0]);
    ImGui::ProgressBar(fraction);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsCombo(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_NewInt32(ctx, 0);
    const char* label = JS_ToCString(ctx, argv[0]);
    int current = toInt32(ctx, argv[1]);
    JS_FreeCString(ctx, label);
    return JS_NewInt32(ctx, current);
}

JSValue JavaScriptEngine::jsTreeNode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    const char* label = JS_ToCString(ctx, argv[0]);
    bool result = ImGui::TreeNode(label);
    JS_FreeCString(ctx, label);
    return result ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsTreePop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ImGui::TreePop();
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsGetScreenSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto& io = ImGui::GetIO();
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, io.DisplaySize.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, io.DisplaySize.y));
    return obj;
}

JSValue JavaScriptEngine::jsGetFrameCount(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewInt32(ctx, ImGui::GetFrameCount());
}

JSValue JavaScriptEngine::jsGetDeltaTime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewFloat64(ctx, ImGui::GetIO().DeltaTime);
}

JSValue JavaScriptEngine::jsIsKeyPressed(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    int key = toInt32(ctx, argv[0]);
    bool repeat = (argc > 1) ? toBool(ctx, argv[1]) : true;
    return ImGui::IsKeyPressed((ImGuiKey)key, repeat) ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsIsMouseClicked(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    int button = (argc > 0) ? toInt32(ctx, argv[0]) : 0;
    bool repeat = (argc > 1) ? toBool(ctx, argv[1]) : false;
    return ImGui::IsMouseClicked(button, repeat) ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsGetMousePos(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto& io = ImGui::GetIO();
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, io.MousePos.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, io.MousePos.y));
    return obj;
}

JSValue JavaScriptEngine::jsLog(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::ostringstream oss;
    for (int i = 0; i < argc; i++) {
        if (i > 0) oss << " ";
        const char* s = JS_ToCString(ctx, argv[i]);
        oss << (s ? s : "undefined");
        if (s) JS_FreeCString(ctx, s);
    }
    if (s_current) s_current->addLog(ScriptLog::Info, oss.str());
    return JS_UNDEFINED;
}

JSValue JavaScriptEngine::jsDrawLine(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 4) return JS_TRUE;
    ImVec2 a((float)toFloat64(ctx, argv[0]), (float)toFloat64(ctx, argv[1]));
    ImVec2 b((float)toFloat64(ctx, argv[2]), (float)toFloat64(ctx, argv[3]));
    ImGui::GetWindowDrawList()->AddLine(a, b, IM_COL32_WHITE, 1.0f);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsDrawRect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 4) return JS_TRUE;
    ImVec2 a((float)toFloat64(ctx, argv[0]), (float)toFloat64(ctx, argv[1]));
    ImVec2 b((float)toFloat64(ctx, argv[2]), (float)toFloat64(ctx, argv[3]));
    ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32_WHITE);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsDrawCircle(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_TRUE;
    ImVec2 center((float)toFloat64(ctx, argv[0]), (float)toFloat64(ctx, argv[1]));
    float radius = (float)toFloat64(ctx, argv[2]);
    ImGui::GetWindowDrawList()->AddCircle(center, radius, IM_COL32_WHITE, 32, 1.0f);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsDrawText(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_TRUE;
    const char* text = JS_ToCString(ctx, argv[0]);
    ImVec2 pos((float)toFloat64(ctx, argv[1]), (float)toFloat64(ctx, argv[2]));
    ImGui::GetWindowDrawList()->AddText(pos, IM_COL32_WHITE, text);
    if (text) JS_FreeCString(ctx, text);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsUnityLoad(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || argc < 1) return JS_NULL;
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_NULL;
    if (s_current->m_unityDumper.isLoading()) {
        JS_FreeCString(ctx, path);
        return JS_ThrowInternalError(ctx, "Unity dump load already in progress");
    }
    std::string pathStr(path);
    JS_FreeCString(ctx, path);
    s_current->m_unityDumper.loadDump(pathStr);
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    s_current->m_pendingUnityPromises.push_back({JS_DupValue(ctx, resolving_funcs[0]), JS_DupValue(ctx, resolving_funcs[1])});
    return promise;
}

JSValue JavaScriptEngine::jsUnityGetAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || argc < 3) return JS_NULL;
    const char* ns = JS_ToCString(ctx, argv[0]);
    const char* className = JS_ToCString(ctx, argv[1]);
    const char* methodName = JS_ToCString(ctx, argv[2]);
    uintptr_t rva = s_current->m_unityDumper.findMethodRVA(ns ? ns : "", className ? className : "", methodName ? methodName : "");
    JS_FreeCString(ctx, ns); JS_FreeCString(ctx, className); JS_FreeCString(ctx, methodName);
    if (rva == 0) return JS_NULL;
    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    uintptr_t base = 0;
    if (!s_current->m_unityModuleName.empty()) base = ms->getModuleBase(s_current->m_unityModuleName);
    if (base == 0) base = ms->getModuleBase("libil2cpp.so");
    if (base == 0) base = ms->getModuleBase("GameAssembly.dll");
    if (base == 0) base = ms->getModuleBase("UnityPlayer.dll");
    if (base == 0) {
        s_current->addLog(ScriptLog::Warning, "Unity module base not found. Returning raw RVA.");
        return JS_NewBigUint64(ctx, rva);
    }
    return JS_NewBigUint64(ctx, base + rva);
}

JSValue JavaScriptEngine::jsUnitySetModuleName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || argc < 1) return JS_FALSE;
    const char* name = JS_ToCString(ctx, argv[0]);
    if (name) {
        s_current->m_unityModuleName = name;
        JS_FreeCString(ctx, name);
        return JS_TRUE;
    }
    return JS_FALSE;
}

JSValue JavaScriptEngine::jsUnityGetModuleBase(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner || argc < 1) return JS_NewBigUint64(ctx, 0);
    const char* name = JS_ToCString(ctx, argv[0]);
    uintptr_t base = static_cast<MemScanner*>(s_current->m_memoryScanner)->getModuleBase(name);
    JS_FreeCString(ctx, name);
    return JS_NewBigUint64(ctx, base);
}

JSValue JavaScriptEngine::jsUnitySearchClasses(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || argc < 1) return JS_NewArray(ctx);
    const char* name = JS_ToCString(ctx, argv[0]);
    auto classes = s_current->m_unityDumper.searchClasses(name ? name : "");
    if (name) JS_FreeCString(ctx, name);
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < classes.size(); ++i) JS_SetPropertyUint32(ctx, arr, i, JS_NewString(ctx, classes[i].c_str()));
    return arr;
}

JSValue JavaScriptEngine::jsUnityListMethods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || argc < 2) return JS_NewArray(ctx);
    const char* ns = JS_ToCString(ctx, argv[0]);
    const char* className = JS_ToCString(ctx, argv[1]);
    auto methods = s_current->m_unityDumper.listMethods(ns ? ns : "", className ? className : "");
    JS_FreeCString(ctx, ns); JS_FreeCString(ctx, className);
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < methods.size(); ++i) JS_SetPropertyUint32(ctx, arr, i, JS_NewString(ctx, methods[i].c_str()));
    return arr;
}

JSValue JavaScriptEngine::jsUnityGetFields(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || argc < 2) return JS_NewArray(ctx);
    const char* ns = JS_ToCString(ctx, argv[0]);
    const char* className = JS_ToCString(ctx, argv[1]);
    
    auto fields = s_current->m_unityDumper.listFields(ns ? ns : "", className ? className : "");
    
    JS_FreeCString(ctx, ns);
    JS_FreeCString(ctx, className);
    
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < fields.size(); ++i) {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, fields[i].name.c_str()));
        JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, fields[i].type.c_str()));
        JS_SetPropertyStr(ctx, obj, "offset", JS_NewBigUint64(ctx, fields[i].offset));
        JS_SetPropertyUint32(ctx, arr, i, obj);
    }
    return arr;
}

JSValue JavaScriptEngine::jsUnityFindObject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // This typically requires scanning the GameObject list in Unity memory.
    // For now, return an empty array or implement a basic AOB/String scan for the name if possible.
    return JS_NewArray(ctx);
}

JSValue JavaScriptEngine::jsUnityGetComponents(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Typically involves reading the Component list from a GameObject address.
    return JS_NewArray(ctx);
}

JSValue JavaScriptEngine::jsUnityIsLoading(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return (s_current && s_current->m_unityDumper.isLoading()) ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsUnityGetLoadProgress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewFloat64(ctx, s_current ? s_current->m_unityDumper.getLoadProgress() : 0.0);
}

JSValue JavaScriptEngine::jsUnityIsLoaded(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return (s_current && s_current->m_unityDumper.isLoaded()) ? JS_TRUE : JS_FALSE;
}

bool JavaScriptEngine::execute(const std::string& code) {
    if (!m_ctx || !m_valid) return false;
    s_current = this;
    JSValue val = JS_Eval(m_ctx, code.c_str(), code.size(), "<script>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(m_ctx);
        const char* str = JS_ToCString(m_ctx, exc);
        m_lastError = str ? str : "Unknown error";
        addLog(ScriptLog::Error, m_lastError);
        if (str) JS_FreeCString(m_ctx, str);
        JS_FreeValue(m_ctx, exc);
        if (m_errorHandler) m_errorHandler(m_lastError);
        JS_FreeValue(m_ctx, val);
        return false;
    }
    JS_FreeValue(m_ctx, val);
    m_lastError.clear();
    JSContext* ctx1;
    while (JS_ExecutePendingJob(m_rt, &ctx1) > 0);
    return true;
}

void JavaScriptEngine::triggerUpdate() {
    if (!m_ctx || !m_valid) return;
    s_current = this;
    if (m_memoryScanner) {
        auto ms = static_cast<MemScanner*>(m_memoryScanner);
        bool isScanning = ms->isScanning();
        if (m_wasScanning && !isScanning) {
            auto results = ms->getResults();
            JSValue resultsArr = JS_NewArray(m_ctx);
            for (uint32_t i = 0; i < results.size(); ++i) JS_SetPropertyUint32(m_ctx, resultsArr, i, JS_NewBigUint64(m_ctx, results[i].address));
            for (auto& promise : m_pendingPromises) {
                JSValue res = JS_Call(m_ctx, promise.resolve, JS_UNDEFINED, 1, &resultsArr);
                JS_FreeValue(m_ctx, res); JS_FreeValue(m_ctx, promise.resolve); JS_FreeValue(m_ctx, promise.reject);
            }
            m_pendingPromises.clear(); JS_FreeValue(m_ctx, resultsArr);
        }
        m_wasScanning = isScanning;
    }
    bool isUnityLoading = m_unityDumper.isLoading();
    if (m_wasUnityLoading && !isUnityLoading) {
        bool success = m_unityDumper.isLoaded();
        JSValue resVal = success ? JS_TRUE : JS_FALSE;
        for (auto& promise : m_pendingUnityPromises) {
            if (success) {
                JSValue res = JS_Call(m_ctx, promise.resolve, JS_UNDEFINED, 1, &resVal);
                JS_FreeValue(m_ctx, res);
            } else {
                JSValue err = JS_NewString(m_ctx, "Failed to load Unity dump");
                JSValue res = JS_Call(m_ctx, promise.reject, JS_UNDEFINED, 1, &err);
                JS_FreeValue(m_ctx, err); JS_FreeValue(m_ctx, res);
            }
            JS_FreeValue(m_ctx, promise.resolve); JS_FreeValue(m_ctx, promise.reject);
        }
        m_pendingUnityPromises.clear();
    }
    m_wasUnityLoading = isUnityLoading;
    JSContext* ctx1;
    while (JS_ExecutePendingJob(m_rt, &ctx1) > 0);
    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue onUpdate = JS_GetPropertyStr(m_ctx, global, "onUpdate");
    if (JS_IsFunction(m_ctx, onUpdate)) {
        JSValue res = JS_Call(m_ctx, onUpdate, global, 0, nullptr);
        if (JS_IsException(res)) {
            JSValue exc = JS_GetException(m_ctx);
            const char* str = JS_ToCString(m_ctx, exc);
            addLog(ScriptLog::Error, std::string("Error in onUpdate: ") + (str ? str : "Unknown"));
            if (str) JS_FreeCString(m_ctx, str);
            JS_FreeValue(m_ctx, exc);
        }
        JS_FreeValue(m_ctx, res);
    }
    JS_FreeValue(m_ctx, onUpdate); JS_FreeValue(m_ctx, global);
}

void JavaScriptEngine::triggerGUI() {
    if (!m_ctx || !m_valid) return;
    s_current = this;

    // Save stack depth
    ImGuiContext& g = *GImGui;
    int windowStackSize = g.CurrentWindowStack.Size;

    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue onGUI = JS_GetPropertyStr(m_ctx, global, "onGUI");
    if (JS_IsFunction(m_ctx, onGUI)) {
        JSValue res = JS_Call(m_ctx, onGUI, global, 0, nullptr);
        if (JS_IsException(res)) {
            JSValue exc = JS_GetException(m_ctx);
            const char* str = JS_ToCString(m_ctx, exc);
            addLog(ScriptLog::Error, std::string("Error in onGUI: ") + (str ? str : "Unknown"));
            if (str) JS_FreeCString(m_ctx, str);
            JS_FreeValue(m_ctx, exc);
        }
        JS_FreeValue(m_ctx, res);
    }
    JS_FreeValue(m_ctx, onGUI);
    JS_FreeValue(m_ctx, global);

    // Force-close any windows left open by the script
    while (g.CurrentWindowStack.Size > windowStackSize) {
        ImGui::End();
    }
}


} // namespace laugh
