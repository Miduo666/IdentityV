#include "SoHookIntegration.h"

#include "EmbeddedHookSo.h"
#include "ImGui/imgui.h"
#include "cJSON.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace SoHook {
namespace {

static_assert(EMBEDDED_HOOK_SO_SIZE == 976280, "内嵌SO大小不匹配");

struct UserRegisters {
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

struct BonePoint {
    float x;
    float y;
    float z;
    bool visible;
};

struct BoneUnit {
    std::string uid;
    std::unordered_map<std::string, BonePoint> points;
};

struct ActionPlayer {
    std::string uid;
    int pid;
    int unitType;
    bool isYidhraPuppet;
    bool isMaryMirror;
    BonePoint position;
};

struct CopycatPlayer {
    std::string uid;
    int roleId;
    int identityId;
    int campId;
    int index;
    BonePoint position;
};

struct CopycatMeetingState {
    bool valid;
    bool isMeeting;
    int sceneType;
    int roundCount;
    std::string sceneName;
};

struct ProgressObject {
    int index;
    BonePoint position;
    float progress;
};

struct PanelObject {
    std::string uid;
    BonePoint position;
    int stateCode;
    bool usable;
    bool spanning;
    float progress;
    float leftTime;
};

std::atomic<bool> listenerRunning{false};
std::atomic<bool> normalPortReady{false};
std::atomic<bool> bonePortReady{false};
std::atomic<int> injectionState{0};
std::atomic<uint64_t> normalPacketCount{0};
std::atomic<uint64_t> bonePacketCount{0};
std::atomic<uint64_t> parseErrorCount{0};
std::atomic<uint64_t> lastPacketAt{0};
std::atomic<uint64_t> lastBoneAt{0};
std::atomic<uint64_t> lastActionAt{0};
std::atomic<uint64_t> lastInfoAt{0};
std::atomic<uint64_t> lastPanelAt{0};
std::atomic<uint64_t> lastCopycatAt{0};
std::atomic<uint64_t> lastCopycatMeetingAt{0};
std::atomic<uint64_t> automaticControlAt{0};
std::atomic<bool> automaticControlPending{false};
std::atomic<bool> controlDataReady{false};
std::atomic<int> trackedTargetPid{0};
std::atomic<uint64_t> lastHookCheckAt{0};
std::thread listenerThread;
std::thread injectionThread;
std::mutex stateMutex;
std::mutex boneMutex;
std::mutex overlayMutex;
std::string listenerError;
std::string injectionMessage = "尚未注入";
std::string lastMessageType = "无";
std::string selfUid = "unknown";
std::vector<BoneUnit> boneUnits;
std::vector<ActionPlayer> actionPlayers;
std::vector<CopycatPlayer> copycatPlayers;
CopycatMeetingState copycatMeetingState{};
std::vector<ProgressObject> generators;
std::vector<ProgressObject> exitGates;
std::vector<ProgressObject> basements;
std::vector<PanelObject> panels;
std::vector<PanelObject> chairs;
std::vector<PanelObject> windows;
bool enableAutoHook = true;
bool needBones = true;
bool needMatrixRaw = true;
bool needInfo = true;
bool needPanels = true;
bool needChairRuntime = true;
bool needSpecialUnits = false;
bool needSpecialUnitBones = false;
bool needCopycat = false;
bool featureAutoSkill = false;
bool featureBoardLowLatency = false;
bool needLowLatencyHunterActions = false;
bool drawBones = true;
bool drawFallbackBoxes = true;
bool drawGenerators = true;
bool drawExitGates = true;
bool drawBasements = true;
bool drawPanels = true;
bool drawChairs = true;
bool drawWindows = false;
bool drawBoneUid = false;
bool ignoreSelfBones = true;

uint64_t NowMilliseconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void SetListenerError(const std::string &message) {
    std::lock_guard<std::mutex> lock(stateMutex);
    listenerError = message;
}

void SetInjectionMessage(const std::string &message) {
    std::lock_guard<std::mutex> lock(stateMutex);
    injectionMessage = message;
}

bool WaitForStop(pid_t targetPid) {
    int status = 0;
    if (waitpid(targetPid, &status, 0) == -1) return false;
    return WIFSTOPPED(status);
}

bool ReadRegisters(pid_t targetPid, UserRegisters &registers) {
    iovec vector{&registers, sizeof(registers)};
    return ptrace(PTRACE_GETREGSET, targetPid, NT_PRSTATUS, &vector) == 0;
}

bool WriteRegisters(pid_t targetPid, const UserRegisters &registers) {
    iovec vector{const_cast<UserRegisters *>(&registers), sizeof(registers)};
    return ptrace(PTRACE_SETREGSET, targetPid, NT_PRSTATUS, &vector) == 0;
}

bool WriteRemoteMemory(pid_t targetPid, uint64_t remoteAddress, const void *data, size_t length) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t offset = 0; offset < length; offset += sizeof(long)) {
        long value = 0;
        const size_t currentLength = std::min(sizeof(long), length - offset);
        if (currentLength != sizeof(long)) {
            errno = 0;
            value = ptrace(PTRACE_PEEKDATA, targetPid, remoteAddress + offset, nullptr);
            if (value == -1 && errno != 0) return false;
        }
        std::memcpy(&value, bytes + offset, currentLength);
        if (ptrace(PTRACE_POKEDATA, targetPid, remoteAddress + offset, value) == -1) return false;
    }
    return true;
}

uint64_t FindModuleBase(pid_t targetPid, const std::string &moduleName) {
    const std::string mapsPath = targetPid == getpid()
        ? "/proc/self/maps"
        : "/proc/" + std::to_string(targetPid) + "/maps";
    std::ifstream maps(mapsPath);
    std::string line;
    uint64_t fallback = 0;
    while (std::getline(maps, line)) {
        if (line.find(moduleName) == std::string::npos) continue;
        uint64_t start = 0;
        uint64_t offset = 0;
        char permissions[5]{};
        if (std::sscanf(line.c_str(), "%lx-%*lx %4s %lx", &start, permissions, &offset) != 3) continue;
        if (!fallback) fallback = start;
        if (offset == 0) return start;
    }
    return fallback;
}

std::string BaseName(const std::string &path) {
    const size_t separator = path.find_last_of('/');
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

uint64_t FindRemoteSymbol(pid_t targetPid, const char *symbolName) {
    void *localSymbol = dlsym(RTLD_DEFAULT, symbolName);
    if (!localSymbol) return 0;
    Dl_info symbolInfo{};
    if (!dladdr(localSymbol, &symbolInfo) || !symbolInfo.dli_fbase || !symbolInfo.dli_fname) return 0;
    const uint64_t remoteBase = FindModuleBase(targetPid, BaseName(symbolInfo.dli_fname));
    if (!remoteBase) return 0;
    return remoteBase + reinterpret_cast<uint64_t>(localSymbol) - reinterpret_cast<uint64_t>(symbolInfo.dli_fbase);
}

bool RemoteCall(pid_t targetPid, uint64_t functionAddress, const uint64_t arguments[8],
                const UserRegisters &baseRegisters, uint64_t &returnValue) {
    UserRegisters callRegisters = baseRegisters;
    for (int index = 0; index < 8; ++index) callRegisters.x[index] = arguments[index];
    callRegisters.pc = functionAddress;
    callRegisters.x[30] = 0;
    if (!WriteRegisters(targetPid, callRegisters)) return false;
    if (ptrace(PTRACE_CONT, targetPid, nullptr, nullptr) == -1 || !WaitForStop(targetPid)) return false;
    UserRegisters resultRegisters{};
    if (!ReadRegisters(targetPid, resultRegisters)) return false;
    returnValue = resultRegisters.x[0];
    return true;
}

bool InjectLibrary(pid_t targetPid, const std::string &path, std::string &error) {
    if (targetPid <= 0) {
        error = "游戏进程号无效";
        return false;
    }
    if (path.empty() || access(path.c_str(), R_OK) != 0) {
        error = "设备上找不到SO: " + path;
        return false;
    }
    if (FindModuleBase(targetPid, BaseName(path))) {
        error = "目标进程已加载该SO";
        return true;
    }
    if (ptrace(PTRACE_ATTACH, targetPid, nullptr, nullptr) == -1) {
        error = std::string("附加游戏进程失败: ") + std::strerror(errno);
        return false;
    }
    if (!WaitForStop(targetPid)) {
        error = std::string("等待游戏进程停止失败: ") + std::strerror(errno);
        ptrace(PTRACE_DETACH, targetPid, nullptr, nullptr);
        return false;
    }

    UserRegisters originalRegisters{};
    const bool registersRead = ReadRegisters(targetPid, originalRegisters);
    bool success = registersRead;
    const uint64_t remoteMmap = FindRemoteSymbol(targetPid, "mmap");
    const uint64_t remoteDlopen = FindRemoteSymbol(targetPid, "dlopen");
    const uint64_t remoteDlsym = FindRemoteSymbol(targetPid, "dlsym");
    if (!success || !remoteMmap || !remoteDlopen || !remoteDlsym) {
        error = "无法解析目标进程的mmap/dlopen/dlsym";
        success = false;
    }

    uint64_t remoteBuffer = 0;
    if (success) {
        const uint64_t mmapArguments[8] = {
            0, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS, static_cast<uint64_t>(-1), 0, 0, 0
        };
        success = RemoteCall(targetPid, remoteMmap, mmapArguments, originalRegisters, remoteBuffer) &&
                  remoteBuffer != 0 && remoteBuffer != UINT64_MAX;
        if (!success) error = "远程mmap失败";
    }
    if (success && !WriteRemoteMemory(targetPid, remoteBuffer, path.c_str(), path.size() + 1)) {
        error = "写入远程SO路径失败";
        success = false;
    }

    uint64_t libraryHandle = 0;
    if (success) {
        const uint64_t dlopenArguments[8] = {remoteBuffer, RTLD_NOW | RTLD_GLOBAL, 0, 0, 0, 0, 0, 0};
        success = RemoteCall(targetPid, remoteDlopen, dlopenArguments, originalRegisters, libraryHandle) && libraryHandle != 0;
        if (!success) error = "目标进程dlopen失败";
    }

    const char entryName[] = "reload_hook";
    if (success && !WriteRemoteMemory(targetPid, remoteBuffer, entryName, sizeof(entryName))) {
        error = "写入reload_hook名称失败";
        success = false;
    }

    uint64_t entryAddress = 0;
    if (success) {
        const uint64_t dlsymArguments[8] = {libraryHandle, remoteBuffer, 0, 0, 0, 0, 0, 0};
        success = RemoteCall(targetPid, remoteDlsym, dlsymArguments, originalRegisters, entryAddress) && entryAddress != 0;
        if (!success) error = "目标进程dlsym(reload_hook)失败";
    }
    if (success) {
        uint64_t ignoredReturnValue = 0;
        const uint64_t entryArguments[8] = {};
        success = RemoteCall(targetPid, entryAddress, entryArguments, originalRegisters, ignoredReturnValue);
        if (!success) error = "远程调用reload_hook失败";
    }

    if (registersRead) WriteRegisters(targetPid, originalRegisters);
    ptrace(PTRACE_DETACH, targetPid, nullptr, nullptr);
    return success;
}

bool WriteEmbeddedLibrary(const std::string &path, std::string &error) {
    const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (file < 0) {
        error = std::string("释放内嵌SO失败: ") + std::strerror(errno);
        return false;
    }
    size_t written = 0;
    while (written < EMBEDDED_HOOK_SO_SIZE) {
        const ssize_t current = write(file, EMBEDDED_HOOK_SO + written, EMBEDDED_HOOK_SO_SIZE - written);
        if (current <= 0) {
            error = std::string("写入内嵌SO失败: ") + std::strerror(errno);
            close(file);
            unlink(path.c_str());
            return false;
        }
        written += static_cast<size_t>(current);
    }
    fsync(file);
    close(file);
    chmod(path.c_str(), 0755);
    return true;
}

bool EnterPermissiveMode() {
    std::ifstream enforceFile("/sys/fs/selinux/enforce");
    int enforcing = 0;
    enforceFile >> enforcing;
    return enforcing == 1 && std::system("setenforce 0 >/dev/null 2>&1") == 0;
}

void RestoreEnforcingMode(bool changed) {
    if (changed) std::system("setenforce 1 >/dev/null 2>&1");
}

int CreateBoundSocket(uint16_t port) {
    const int socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) return -1;
    int receiveBufferSize = 512 * 1024;
    setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, &receiveBufferSize, sizeof(receiveBufferSize));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(socketFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1) {
        close(socketFd);
        return -1;
    }
    const int flags = fcntl(socketFd, F_GETFL, 0);
    fcntl(socketFd, F_SETFL, flags | O_NONBLOCK);
    return socketFd;
}

bool SendPacket(const std::string &packet) {
    const int socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(55556);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    const ssize_t sent = sendto(socketFd, packet.data(), packet.size(), 0,
                                reinterpret_cast<sockaddr *>(&address), sizeof(address));
    close(socketFd);
    return sent == static_cast<ssize_t>(packet.size());
}

std::string BuildControlPacket() {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hook_control");
    cJSON_AddBoolToObject(root, "enable_auto_hook", enableAutoHook);
    cJSON_AddBoolToObject(root, "need_bones", needBones);
    cJSON_AddBoolToObject(root, "need_matrix_raw", needMatrixRaw);
    cJSON_AddBoolToObject(root, "need_info", needInfo);
    cJSON_AddBoolToObject(root, "need_panels", needPanels);
    cJSON_AddBoolToObject(root, "need_chair_runtime", needChairRuntime);
    cJSON_AddBoolToObject(root, "need_panel_static_cache", needPanels);
    cJSON_AddBoolToObject(root, "need_special_units", needSpecialUnits);
    cJSON_AddBoolToObject(root, "need_special_unit_bones", needSpecialUnitBones);
    cJSON_AddBoolToObject(root, "need_copycat_advanced", needCopycat);
    cJSON_AddBoolToObject(root, "need_copycat_meeting", needCopycat);
    cJSON_AddBoolToObject(root, "feature_auto_skill", featureAutoSkill);
    cJSON_AddBoolToObject(root, "feature_board_low_latency", featureBoardLowLatency);
    cJSON_AddBoolToObject(root, "need_low_latency_hunter_actions", needLowLatencyHunterActions);
    char *text = cJSON_PrintUnformatted(root);
    std::string packet = text ? text : "";
    cJSON_free(text);
    cJSON_Delete(root);
    return packet;
}

void ParseBonePacket(cJSON *root) {
    cJSON *dataNode = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsObject(dataNode)) return;

    std::vector<BoneUnit> parsedUnits;
    for (cJSON *unitNode = dataNode->child; unitNode && parsedUnits.size() < 64; unitNode = unitNode->next) {
        if (!unitNode->string || !cJSON_IsObject(unitNode)) continue;
        BoneUnit unit;
        unit.uid = unitNode->string;
        for (cJSON *boneNode = unitNode->child; boneNode; boneNode = boneNode->next) {
            if (!boneNode->string || !cJSON_IsObject(boneNode)) continue;
            cJSON *worldNode = cJSON_GetObjectItemCaseSensitive(boneNode, "world");
            if (!cJSON_IsArray(worldNode) || cJSON_GetArraySize(worldNode) < 3) continue;
            cJSON *xNode = cJSON_GetArrayItem(worldNode, 0);
            cJSON *yNode = cJSON_GetArrayItem(worldNode, 1);
            cJSON *zNode = cJSON_GetArrayItem(worldNode, 2);
            if (!cJSON_IsNumber(xNode) || !cJSON_IsNumber(yNode) || !cJSON_IsNumber(zNode)) continue;
            cJSON *visibleNode = cJSON_GetObjectItemCaseSensitive(boneNode, "visible");
            unit.points.emplace(boneNode->string, BonePoint{
                static_cast<float>(xNode->valuedouble),
                static_cast<float>(yNode->valuedouble),
                static_cast<float>(zNode->valuedouble),
                cJSON_IsTrue(visibleNode) != 0
            });
        }
        if (!unit.points.empty()) parsedUnits.push_back(std::move(unit));
    }

    cJSON *selfNode = cJSON_GetObjectItemCaseSensitive(root, "self_uid");
    std::lock_guard<std::mutex> lock(boneMutex);
    if (cJSON_IsString(selfNode) && selfNode->valuestring) selfUid = selfNode->valuestring;
    boneUnits = std::move(parsedUnits);
    lastBoneAt.store(NowMilliseconds());
}

void ParseActionPacket(cJSON *root) {
    cJSON *playersNode = cJSON_GetObjectItemCaseSensitive(root, "players");
    if (!cJSON_IsArray(playersNode)) return;
    std::vector<ActionPlayer> parsedPlayers;
    const int playerCount = std::min(cJSON_GetArraySize(playersNode), 64);
    for (int index = 0; index < playerCount; ++index) {
        cJSON *playerNode = cJSON_GetArrayItem(playersNode, index);
        if (!cJSON_IsObject(playerNode)) continue;
        cJSON *positionNode = cJSON_GetObjectItemCaseSensitive(playerNode, "pos");
        if (!cJSON_IsArray(positionNode) || cJSON_GetArraySize(positionNode) < 3) continue;
        cJSON *xNode = cJSON_GetArrayItem(positionNode, 0);
        cJSON *yNode = cJSON_GetArrayItem(positionNode, 1);
        cJSON *zNode = cJSON_GetArrayItem(positionNode, 2);
        if (!cJSON_IsNumber(xNode) || !cJSON_IsNumber(yNode) || !cJSON_IsNumber(zNode)) continue;
        cJSON *uidNode = cJSON_GetObjectItemCaseSensitive(playerNode, "uid");
        cJSON *pidNode = cJSON_GetObjectItemCaseSensitive(playerNode, "pid");
        cJSON *unitTypeNode = cJSON_GetObjectItemCaseSensitive(playerNode, "unit_type");
        cJSON *puppetNode = cJSON_GetObjectItemCaseSensitive(playerNode, "is_yidhra_puppet");
        cJSON *mirrorNode = cJSON_GetObjectItemCaseSensitive(playerNode, "is_mary_mirror");
        parsedPlayers.push_back(ActionPlayer{
            cJSON_IsString(uidNode) && uidNode->valuestring ? uidNode->valuestring : "unknown",
            cJSON_IsNumber(pidNode) ? pidNode->valueint : 0,
            cJSON_IsNumber(unitTypeNode) ? unitTypeNode->valueint : 0,
            cJSON_IsTrue(puppetNode) != 0,
            cJSON_IsTrue(mirrorNode) != 0,
            BonePoint{
                static_cast<float>(xNode->valuedouble),
                static_cast<float>(yNode->valuedouble),
                static_cast<float>(zNode->valuedouble),
                true
            }
        });
    }
    cJSON *selfNode = cJSON_GetObjectItemCaseSensitive(root, "self_uid");
    if (cJSON_IsString(selfNode) && selfNode->valuestring) {
        std::lock_guard<std::mutex> boneLock(boneMutex);
        selfUid = selfNode->valuestring;
    }
    {
        std::lock_guard<std::mutex> lock(overlayMutex);
        actionPlayers = std::move(parsedPlayers);
    }
    lastActionAt.store(NowMilliseconds());
}

bool ParseInfoPacket(const char *packet, size_t length) {
    const std::string text(packet, length);
    if (text.find("发电机数量:") == std::string::npos && text.find("大门数量:") == std::string::npos) return false;

    std::vector<ProgressObject> parsedGenerators;
    std::vector<ProgressObject> parsedGates;
    std::vector<ProgressObject> parsedBasements;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        ProgressObject object{};
        if (std::sscanf(line.c_str(), "电机%d [X:%f,Y:%f,Z:%f] 进度:%f%%",
                        &object.index, &object.position.x, &object.position.y, &object.position.z,
                        &object.progress) == 5) {
            object.position.visible = true;
            parsedGenerators.push_back(object);
            continue;
        }
        if (std::sscanf(line.c_str(), "大门%d [X:%f,Y:%f,Z:%f] 进度:%f%%",
                        &object.index, &object.position.x, &object.position.y, &object.position.z,
                        &object.progress) == 5) {
            object.position.visible = true;
            parsedGates.push_back(object);
            continue;
        }
        if (std::sscanf(line.c_str(), "地下室%d [X:%f,Y:%f,Z:%f]",
                        &object.index, &object.position.x, &object.position.y, &object.position.z) == 4) {
            object.position.visible = true;
            parsedBasements.push_back(object);
        }
    }
    {
        std::lock_guard<std::mutex> lock(overlayMutex);
        generators = std::move(parsedGenerators);
        exitGates = std::move(parsedGates);
        basements = std::move(parsedBasements);
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        lastMessageType = "info";
    }
    lastInfoAt.store(NowMilliseconds());
    return true;
}

void MarkControlDataReady() {
    if (!controlDataReady.exchange(true)) {
        automaticControlPending.store(false);
        SetInjectionMessage("SO数据接收正常，停止自动发送配置");
    }
}

bool ReadPosition(cJSON *objectNode, BonePoint &position) {
    cJSON *positionNode = cJSON_GetObjectItemCaseSensitive(objectNode, "pos");
    if (!cJSON_IsArray(positionNode) || cJSON_GetArraySize(positionNode) < 3) return false;
    cJSON *xNode = cJSON_GetArrayItem(positionNode, 0);
    cJSON *yNode = cJSON_GetArrayItem(positionNode, 1);
    cJSON *zNode = cJSON_GetArrayItem(positionNode, 2);
    if (!cJSON_IsNumber(xNode) || !cJSON_IsNumber(yNode) || !cJSON_IsNumber(zNode)) return false;
    position = BonePoint{
        static_cast<float>(xNode->valuedouble),
        static_cast<float>(yNode->valuedouble),
        static_cast<float>(zNode->valuedouble),
        true
    };
    return true;
}

void ParsePanelPacket(cJSON *root) {
    std::unordered_map<std::string, PanelObject> panelStates;
    cJSON *panelsNode = cJSON_GetObjectItemCaseSensitive(root, "panels");
    if (cJSON_IsArray(panelsNode)) {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, panelsNode) {
            cJSON *uidNode = cJSON_GetObjectItemCaseSensitive(item, "uid");
            if (!cJSON_IsString(uidNode) || !uidNode->valuestring) continue;
            cJSON *stateNode = cJSON_GetObjectItemCaseSensitive(item, "state_code");
            cJSON *usableNode = cJSON_GetObjectItemCaseSensitive(item, "usable");
            cJSON *spanningNode = cJSON_GetObjectItemCaseSensitive(item, "spanning");
            panelStates[uidNode->valuestring] = PanelObject{
                uidNode->valuestring, BonePoint{},
                cJSON_IsNumber(stateNode) ? stateNode->valueint : -1,
                cJSON_IsTrue(usableNode) != 0,
                cJSON_IsTrue(spanningNode) != 0,
                -1.0f, -1.0f
            };
        }
    }

    std::vector<PanelObject> parsedPanels;
    cJSON *staticPanelsNode = cJSON_GetObjectItemCaseSensitive(root, "static_panels");
    if (cJSON_IsArray(staticPanelsNode)) {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, staticPanelsNode) {
            cJSON *uidNode = cJSON_GetObjectItemCaseSensitive(item, "uid");
            if (!cJSON_IsString(uidNode) || !uidNode->valuestring) continue;
            BonePoint position{};
            if (!ReadPosition(item, position)) continue;
            auto state = panelStates.find(uidNode->valuestring);
            PanelObject panel = state != panelStates.end()
                ? state->second
                : PanelObject{uidNode->valuestring, BonePoint{}, -1, false, false, -1.0f, -1.0f};
            panel.position = position;
            parsedPanels.push_back(std::move(panel));
        }
    }

    std::vector<PanelObject> parsedChairs;
    cJSON *chairsNode = cJSON_GetObjectItemCaseSensitive(root, "chairs");
    if (cJSON_IsArray(chairsNode)) {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, chairsNode) {
            BonePoint position{};
            if (!ReadPosition(item, position)) continue;
            cJSON *uidNode = cJSON_GetObjectItemCaseSensitive(item, "uid");
            cJSON *stateNode = cJSON_GetObjectItemCaseSensitive(item, "state_code");
            cJSON *progressNode = cJSON_GetObjectItemCaseSensitive(item, "hook_progress_percent");
            cJSON *leftTimeNode = cJSON_GetObjectItemCaseSensitive(item, "left_time_seconds");
            parsedChairs.push_back(PanelObject{
                cJSON_IsString(uidNode) && uidNode->valuestring ? uidNode->valuestring : "unknown",
                position,
                cJSON_IsNumber(stateNode) ? stateNode->valueint : -1,
                false, false,
                cJSON_IsNumber(progressNode) ? static_cast<float>(progressNode->valuedouble) : -1.0f,
                cJSON_IsNumber(leftTimeNode) ? static_cast<float>(leftTimeNode->valuedouble) : -1.0f
            });
        }
    }

    std::vector<PanelObject> parsedWindows;
    cJSON *windowsNode = cJSON_GetObjectItemCaseSensitive(root, "windows");
    if (cJSON_IsArray(windowsNode)) {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, windowsNode) {
            BonePoint position{};
            if (!ReadPosition(item, position)) continue;
            cJSON *uidNode = cJSON_GetObjectItemCaseSensitive(item, "uid");
            parsedWindows.push_back(PanelObject{
                cJSON_IsString(uidNode) && uidNode->valuestring ? uidNode->valuestring : "unknown",
                position, -1, true, false, -1.0f, -1.0f
            });
        }
    }

    {
        std::lock_guard<std::mutex> lock(overlayMutex);
        panels = std::move(parsedPanels);
        chairs = std::move(parsedChairs);
        windows = std::move(parsedWindows);
    }
    lastPanelAt.store(NowMilliseconds());
}

bool IsKnownCopycatRoleId(int roleId) {
    return (roleId >= 101 && roleId <= 118) ||
           (roleId >= 201 && roleId <= 209) ||
           (roleId >= 301 && roleId <= 308);
}

void ParseCopycatAdvancedPacket(cJSON *root) {
    cJSON *playersNode = cJSON_GetObjectItemCaseSensitive(root, "players");
    if (!cJSON_IsArray(playersNode)) return;

    std::vector<CopycatPlayer> parsedPlayers;
    const int playerCount = std::min(cJSON_GetArraySize(playersNode), 64);
    for (int index = 0; index < playerCount; ++index) {
        cJSON *playerNode = cJSON_GetArrayItem(playersNode, index);
        if (!cJSON_IsObject(playerNode)) continue;

        cJSON *uidNode = cJSON_GetObjectItemCaseSensitive(playerNode, "uid");
        cJSON *roleNode = cJSON_GetObjectItemCaseSensitive(playerNode, "role_id");
        cJSON *identityNode = cJSON_GetObjectItemCaseSensitive(playerNode, "identity_id");
        cJSON *campNode = cJSON_GetObjectItemCaseSensitive(playerNode, "camp_id");
        cJSON *indexNode = cJSON_GetObjectItemCaseSensitive(playerNode, "idx");
        cJSON *xNode = cJSON_GetObjectItemCaseSensitive(playerNode, "x");
        cJSON *yNode = cJSON_GetObjectItemCaseSensitive(playerNode, "y");
        cJSON *zNode = cJSON_GetObjectItemCaseSensitive(playerNode, "z");
        if (!cJSON_IsNumber(xNode) || !cJSON_IsNumber(yNode) || !cJSON_IsNumber(zNode)) continue;

        const int roleId = cJSON_IsNumber(roleNode) ? roleNode->valueint : -1;
        const int identityId = cJSON_IsNumber(identityNode) ? identityNode->valueint : -1;
        const int effectiveRoleId = identityId > 0 ? identityId : roleId;
        if (!IsKnownCopycatRoleId(effectiveRoleId)) continue;
        int campId = cJSON_IsNumber(campNode) ? campNode->valueint : -1;
        if (campId < 1 || campId > 3) campId = effectiveRoleId / 100;

        parsedPlayers.push_back(CopycatPlayer{
            cJSON_IsString(uidNode) && uidNode->valuestring ? uidNode->valuestring : "unknown",
            roleId,
            identityId,
            campId,
            cJSON_IsNumber(indexNode) ? indexNode->valueint : -1,
            BonePoint{
                static_cast<float>(xNode->valuedouble),
                static_cast<float>(yNode->valuedouble),
                static_cast<float>(zNode->valuedouble),
                true
            }
        });
    }

    {
        std::lock_guard<std::mutex> lock(overlayMutex);
        copycatPlayers = std::move(parsedPlayers);
    }
    lastCopycatAt.store(NowMilliseconds());
}

void ParseCopycatMeetingPacket(cJSON *root) {
    cJSON *validNode = cJSON_GetObjectItemCaseSensitive(root, "valid");
    cJSON *meetingNode = cJSON_GetObjectItemCaseSensitive(root, "is_meeting");
    cJSON *sceneTypeNode = cJSON_GetObjectItemCaseSensitive(root, "scene_type");
    cJSON *sceneNameNode = cJSON_GetObjectItemCaseSensitive(root, "scene_name");
    cJSON *roundCountNode = cJSON_GetObjectItemCaseSensitive(root, "round_count");

    CopycatMeetingState parsedState{};
    parsedState.valid = cJSON_IsTrue(validNode) != 0;
    parsedState.isMeeting = cJSON_IsTrue(meetingNode) != 0;
    parsedState.sceneType = cJSON_IsNumber(sceneTypeNode) ? sceneTypeNode->valueint : -1;
    parsedState.roundCount = cJSON_IsNumber(roundCountNode) ? roundCountNode->valueint : -1;
    parsedState.sceneName = cJSON_IsString(sceneNameNode) && sceneNameNode->valuestring
        ? sceneNameNode->valuestring : "";
    {
        std::lock_guard<std::mutex> lock(overlayMutex);
        copycatMeetingState = std::move(parsedState);
    }
    lastCopycatMeetingAt.store(NowMilliseconds());
}

void ParsePacket(const char *packet, size_t length, uint16_t port) {
    lastPacketAt.store(NowMilliseconds());
    if (port == 55557) bonePacketCount.fetch_add(1);
    else normalPacketCount.fetch_add(1);
    cJSON *root = cJSON_ParseWithLength(packet, length);
    if (!root) {
        if (port == 55555 && ParseInfoPacket(packet, length)) {
            MarkControlDataReady();
            return;
        }
        parseErrorCount.fetch_add(1);
        return;
    }
    cJSON *typeNode = cJSON_GetObjectItemCaseSensitive(root, "type");
    const std::string messageType = cJSON_IsString(typeNode) && typeNode->valuestring
        ? typeNode->valuestring : "未知";
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        lastMessageType = messageType;
    }
    if (messageType == "bones") ParseBonePacket(root);
    else if (messageType == "actions") ParseActionPacket(root);
    else if (messageType == "panels") ParsePanelPacket(root);
    else if (messageType == "copycat_advanced") ParseCopycatAdvancedPacket(root);
    else if (messageType == "copycat_meeting_state") ParseCopycatMeetingPacket(root);
    if (messageType == "bones" || messageType == "actions" || messageType == "panels" ||
        messageType == "matrix_raw" || messageType == "copycat_advanced" ||
        messageType == "copycat_meeting_state") {
        MarkControlDataReady();
    }
    cJSON_Delete(root);
}

void DrainSocket(int socketFd, uint16_t port) {
    char buffer[65536];
    while (true) {
        const ssize_t length = recvfrom(socketFd, buffer, sizeof(buffer), MSG_DONTWAIT, nullptr, nullptr);
        if (length <= 0) break;
        ParsePacket(buffer, static_cast<size_t>(length), port);
    }
}

void ListenerLoop() {
    const int normalSocket = CreateBoundSocket(55555);
    const int boneSocket = CreateBoundSocket(55557);
    normalPortReady.store(normalSocket >= 0);
    bonePortReady.store(boneSocket >= 0);
    if (normalSocket < 0 || boneSocket < 0) {
        SetListenerError(std::string("端口绑定失败: ") +
            (normalSocket < 0 ? "55555 " : "") + (boneSocket < 0 ? "55557" : ""));
    } else {
        SetListenerError("");
        std::printf("[SO监听] 已监听 127.0.0.1:55555 和 127.0.0.1:55557\n");
    }

    while (listenerRunning.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        int maxFd = -1;
        if (normalSocket >= 0) {
            FD_SET(normalSocket, &readSet);
            maxFd = std::max(maxFd, normalSocket);
        }
        if (boneSocket >= 0) {
            FD_SET(boneSocket, &readSet);
            maxFd = std::max(maxFd, boneSocket);
        }
        timeval timeout{0, 100000};
        if (maxFd < 0) {
            usleep(100000);
            continue;
        }
        const int ready = select(maxFd + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0) continue;
        if (normalSocket >= 0 && FD_ISSET(normalSocket, &readSet)) DrainSocket(normalSocket, 55555);
        if (boneSocket >= 0 && FD_ISSET(boneSocket, &readSet)) DrainSocket(boneSocket, 55557);
    }

    if (normalSocket >= 0) close(normalSocket);
    if (boneSocket >= 0) close(boneSocket);
    normalPortReady.store(false);
    bonePortReady.store(false);
}

void StartInjection(int targetPid) {
    if (injectionState.load() == 1) return;
    if (injectionThread.joinable()) injectionThread.join();
    controlDataReady.store(false);
    injectionState.store(1);
    SetInjectionMessage("正在释放内嵌SO");
    injectionThread = std::thread([targetPid]() {
        const std::string path = "/data/local/tmp/.idv_hook_" + std::to_string(targetPid) + ".so";
        std::string message;
        bool success = WriteEmbeddedLibrary(path, message);
        const bool restoreSelinux = success && EnterPermissiveMode();
        if (success) {
            SetInjectionMessage("正在附加并调用reload_hook");
            success = InjectLibrary(targetPid, path, message);
        }
        unlink(path.c_str());
        RestoreEnforcingMode(restoreSelinux);
        if (success) {
            injectionState.store(2);
            SetInjectionMessage(message == "目标进程已加载该SO" ? message : "注入成功，等待脚本启动");
            automaticControlAt.store(NowMilliseconds() + 1200);
            automaticControlPending.store(true);
            std::printf("[SO注入] %s\n", message.empty() ? "成功" : message.c_str());
        } else {
            injectionState.store(3);
            SetInjectionMessage(message);
            std::printf("[SO注入错误] %s\n", message.c_str());
        }
    });
}

bool ProjectPoint(const BonePoint &point, const float matrix[16], float centerX, float centerY, ImVec2 &screen) {
    const float camera = matrix[3] * point.x + matrix[7] * point.y + matrix[11] * point.z + matrix[15];
    if (!std::isfinite(camera) || camera <= 0.01f) return false;
    screen.x = centerX + (matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12]) / camera * centerX;
    screen.y = centerY - (matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13]) / camera * centerY;
    return std::isfinite(screen.x) && std::isfinite(screen.y) &&
           screen.x > -centerX && screen.x < centerX * 3.0f &&
           screen.y > -centerY && screen.y < centerY * 3.0f;
}

const char *CopycatRoleName(int roleId) {
    switch (roleId) {
        case 101: return "侦探";
        case 102: return "哨兵";
        case 103: return "治安官";
        case 104: return "猎人";
        case 105: return "香料师";
        case 106: return "锁匠";
        case 107: return "银行家";
        case 108: return "修士";
        case 109: return "演说家";
        case 110: return "拳击手";
        case 111: return "灵媒";
        case 112: return "学徒";
        case 113: return "密探";
        case 114: return "掮客";
        case 115: return "药剂师";
        case 116: return "巡林员";
        case 117: return "执灯人";
        case 118: return "评论家";
        case 201: return "神偷";
        case 202: return "千面人";
        case 203: return "阴谋家";
        case 204: return "烟火师";
        case 205: return "怪盗";
        case 206: return "催眠师";
        case 207: return "地下医生";
        case 208: return "处刑人";
        case 209: return "指挥家";
        case 301: return "流浪汉";
        case 302: return "送货员";
        case 303: return "愚人";
        case 304: return "棋手";
        case 305: return "清洁工";
        case 306: return "顾问";
        case 307: return "降灵师";
        case 308: return "导演";
        default: return "未知身份";
    }
}

int CopycatCampId(const CopycatPlayer &player) {
    if (player.campId >= 1 && player.campId <= 3) return player.campId;
    const int roleId = player.identityId > 0 ? player.identityId : player.roleId;
    if (roleId >= 100 && roleId < 200) return 1;
    if (roleId >= 200 && roleId < 300) return 2;
    if (roleId >= 300 && roleId < 400) return 3;
    return -1;
}

const char *CopycatCampName(int campId) {
    if (campId == 1) return "侦探团";
    if (campId == 2) return "模仿者";
    if (campId == 3) return "神秘客";
    return "未知阵营";
}

ImU32 CopycatCampColor(int campId) {
    if (campId == 1) return IM_COL32(194, 208, 235, 245);
    if (campId == 2) return IM_COL32(227, 112, 128, 245);
    if (campId == 3) return IM_COL32(240, 220, 120, 245);
    return IM_COL32(255, 255, 255, 235);
}

}

void StartListeners() {
    if (listenerRunning.exchange(true)) return;
    listenerThread = std::thread(ListenerLoop);
}

void StopListeners() {
    listenerRunning.store(false);
    if (listenerThread.joinable()) listenerThread.join();
    if (injectionThread.joinable()) injectionThread.join();
}

bool IsCopycatDrawingActive() {
    const uint64_t copycatTime = lastCopycatAt.load();
    if (!needCopycat || !copycatTime || NowMilliseconds() - copycatTime > 5000) return false;
    std::lock_guard<std::mutex> lock(overlayMutex);
    return !copycatPlayers.empty();
}

void Update(int targetPid) {
    if (targetPid <= 0) return;

    const int previousPid = trackedTargetPid.exchange(targetPid);
    if (previousPid != targetPid) {
        lastHookCheckAt.store(0);
        automaticControlPending.store(false);
        controlDataReady.store(false);
        if (injectionState.load() != 1) {
            injectionState.store(0);
            SetInjectionMessage("尚未注入");
        }
    }

    const uint64_t now = NowMilliseconds();
    const int currentState = injectionState.load();
    const uint64_t previousCheck = lastHookCheckAt.load();
    if (currentState != 1 && currentState != 2 && now - previousCheck >= 1000) {
        lastHookCheckAt.store(now);
        if (FindModuleBase(targetPid, ".idv_hook_") != 0) {
            injectionState.store(2);
            SetInjectionMessage("检测到SO已注入，正在自动发送采集配置");
            controlDataReady.store(false);
            automaticControlAt.store(now + 300);
            automaticControlPending.store(true);
        }
    }

    if (controlDataReady.load()) {
        automaticControlPending.store(false);
        return;
    }

    if (!automaticControlPending.load() || now < automaticControlAt.load() ||
        !normalPortReady.load() || !bonePortReady.load()) {
        return;
    }

    const std::string packet = BuildControlPacket();
    const bool sent = !packet.empty() && SendPacket(packet);
    automaticControlAt.store(now + 3000);
    automaticControlPending.store(true);
    SetInjectionMessage(sent ? "采集配置已自动发送，等待SO数据" : "采集配置自动发送失败，稍后重试");
}

void RenderPanel(int targetPid) {
    Update(targetPid);

    const bool portsReady = normalPortReady.load() && bonePortReady.load();
    ImGui::TextColored(portsReady ? ImVec4(0.2f, 0.9f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.2f, 1.0f),
                       "监听端口: 55555 %s / 55557 %s",
                       normalPortReady.load() ? "正常" : "失败",
                       bonePortReady.load() ? "正常" : "失败");
    ImGui::Text("内嵌SO: %u 字节，临时放宽SELinux后注入并清理", EMBEDDED_HOOK_SO_SIZE);
    const int currentInjectionState = injectionState.load();
    if (currentInjectionState != 2) {
        ImGui::BeginDisabled(targetPid <= 0 || currentInjectionState == 1);
        if (ImGui::Button(currentInjectionState == 1 ? "正在注入" : "注入SO")) StartInjection(targetPid);
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(targetPid <= 0);
    if (ImGui::Button("发送采集配置")) {
        const std::string packet = BuildControlPacket();
        SetInjectionMessage(!packet.empty() && SendPacket(packet) ? "采集配置发送成功" : "采集配置发送失败");
    }
    ImGui::EndDisabled();

    std::string currentInjectionMessage;
    std::string currentListenerError;
    std::string currentLastType;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentInjectionMessage = injectionMessage;
        currentListenerError = listenerError;
        currentLastType = lastMessageType;
    }
    ImGui::TextWrapped("注入状态: %s", currentInjectionMessage.c_str());
    if (!currentListenerError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.2f, 1.0f), "%s", currentListenerError.c_str());

    ImGui::Separator();
    ImGui::Text("SO数据采集");
    ImGui::Checkbox("自动Hook", &enableAutoHook);
    ImGui::SameLine();
    ImGui::Checkbox("人物骨骼", &needBones);
    ImGui::SameLine();
    ImGui::Checkbox("相机矩阵", &needMatrixRaw);
    ImGui::Checkbox("玩家信息", &needInfo);
    ImGui::SameLine();
    ImGui::Checkbox("板窗数据", &needPanels);
    ImGui::SameLine();
    ImGui::Checkbox("椅子状态", &needChairRuntime);
    ImGui::Checkbox("特殊单位", &needSpecialUnits);
    ImGui::SameLine();
    ImGui::Checkbox("特殊单位骨骼", &needSpecialUnitBones);
    if (ImGui::Checkbox("模仿者模式", &needCopycat)) {
        if (currentInjectionState == 2) {
            const std::string packet = BuildControlPacket();
            SetInjectionMessage(!packet.empty() && SendPacket(packet)
                ? (needCopycat ? "模仿者采集已开启" : "模仿者采集已关闭")
                : "模仿者采集配置发送失败");
        } else {
            SetInjectionMessage("模仿者设置已保存，注入后自动发送");
        }
    }

    ImGui::Separator();
    ImGui::Text("SO功能");
    ImGui::Checkbox("自动技能数据", &featureAutoSkill);
    ImGui::SameLine();
    ImGui::Checkbox("板窗低延迟", &featureBoardLowLatency);
    ImGui::Checkbox("监管动作低延迟", &needLowLatencyHunterActions);
    ImGui::Checkbox("绘制SO骨骼", &drawBones);
    ImGui::SameLine();
    ImGui::Checkbox("显示骨骼UID", &drawBoneUid);
    ImGui::SameLine();
    ImGui::Checkbox("忽略自身骨骼", &ignoreSelfBones);
    ImGui::Checkbox("内核失效时SO人物框", &drawFallbackBoxes);
    ImGui::Checkbox("绘制密码机进度", &drawGenerators);
    ImGui::SameLine();
    ImGui::Checkbox("绘制大门进度", &drawExitGates);
    ImGui::SameLine();
    ImGui::Checkbox("绘制地窖", &drawBasements);
    ImGui::Checkbox("绘制木板状态", &drawPanels);
    ImGui::SameLine();
    ImGui::Checkbox("绘制椅子状态", &drawChairs);
    ImGui::SameLine();
    ImGui::Checkbox("绘制窗户", &drawWindows);
    if (ImGui::Button("强制快速翻板")) SetInjectionMessage(SendPacket("FORCE_FAST_VAULT") ? "快速翻板命令已发送" : "快速翻板命令发送失败");
    ImGui::SameLine();
    if (ImGui::Button("触发特殊无敌")) SetInjectionMessage(SendPacket("FORCE_INVINCIBILITY") ? "无敌命令已发送" : "无敌命令发送失败");

    size_t currentBoneUnits = 0;
    size_t currentActionPlayers = 0;
    size_t currentGenerators = 0;
    size_t currentGates = 0;
    size_t currentPanels = 0;
    size_t currentChairs = 0;
    size_t currentWindows = 0;
    size_t currentCopycatPlayers = 0;
    CopycatMeetingState currentCopycatMeeting{};
    std::string currentSelfUid;
    {
        std::lock_guard<std::mutex> lock(boneMutex);
        currentBoneUnits = boneUnits.size();
        currentSelfUid = selfUid;
    }
    {
        std::lock_guard<std::mutex> lock(overlayMutex);
        currentActionPlayers = actionPlayers.size();
        currentGenerators = generators.size();
        currentGates = exitGates.size();
        currentPanels = panels.size();
        currentChairs = chairs.size();
        currentWindows = windows.size();
        currentCopycatPlayers = copycatPlayers.size();
        currentCopycatMeeting = copycatMeetingState;
    }
    const uint64_t packetTime = lastPacketAt.load();
    const uint64_t packetAge = packetTime ? NowMilliseconds() - packetTime : 0;
    const uint64_t copycatMeetingTime = lastCopycatMeetingAt.load();
    const bool copycatMeetingFresh = copycatMeetingTime && NowMilliseconds() - copycatMeetingTime <= 5000;
    const char *copycatStateText = !needCopycat ? "未开启"
        : (!copycatMeetingFresh || !currentCopycatMeeting.valid ? "等待数据"
        : (currentCopycatMeeting.isMeeting ? "会议中" : "自由行动"));
    ImGui::Separator();
    ImGui::Text("最后消息: %s%s", currentLastType.c_str(), packetTime ? "" : "（尚未收到）");
    ImGui::Text("普通包: %llu  骨骼包: %llu  解析失败: %llu",
                static_cast<unsigned long long>(normalPacketCount.load()),
                static_cast<unsigned long long>(bonePacketCount.load()),
                static_cast<unsigned long long>(parseErrorCount.load()));
    ImGui::Text("骨骼人物: %zu  自身UID: %s", currentBoneUnits, currentSelfUid.c_str());
    ImGui::Text("SO人物: %zu  密码机: %zu  大门: %zu", currentActionPlayers, currentGenerators, currentGates);
    ImGui::Text("木板: %zu  椅子: %zu  窗户: %zu", currentPanels, currentChairs, currentWindows);
    ImGui::Text("模仿者人物: %zu  状态: %s", currentCopycatPlayers, copycatStateText);
    if (packetTime) ImGui::Text("最后数据: %llu ms前", static_cast<unsigned long long>(packetAge));
}

void DrawOverlay(ImDrawList *drawList, const float viewProjection[16], float centerX, float centerY,
                 int kernelPlayerCount, float playerX, float playerY, float playerZ, float unitsPerMeter) {
    if (!drawList || !viewProjection || centerX <= 0 || centerY <= 0) return;

    const bool hasPlayerPosition = std::isfinite(playerX) && std::isfinite(playerY) &&
        std::isfinite(playerZ) && unitsPerMeter > 0.0f &&
        (std::fabs(playerX) + std::fabs(playerY) + std::fabs(playerZ) > 0.01f);
    auto distanceToPlayer = [&](const BonePoint &point) {
        if (!hasPlayerPosition) return INFINITY;
        const float deltaX = point.x - playerX;
        const float deltaY = point.y - playerY;
        const float deltaZ = point.z - playerZ;
        return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ) / unitsPerMeter;
    };

    std::vector<ActionPlayer> currentPlayers;
    std::vector<CopycatPlayer> currentCopycatPlayers;
    std::vector<ProgressObject> currentGenerators;
    std::vector<ProgressObject> currentGates;
    std::vector<ProgressObject> currentBasements;
    std::vector<PanelObject> currentPanels;
    std::vector<PanelObject> currentChairs;
    std::vector<PanelObject> currentWindows;
    {
        std::lock_guard<std::mutex> lock(overlayMutex);
        currentPlayers = actionPlayers;
        currentCopycatPlayers = copycatPlayers;
        currentGenerators = generators;
        currentGates = exitGates;
        currentBasements = basements;
        currentPanels = panels;
        currentChairs = chairs;
        currentWindows = windows;
    }
    const uint64_t now = NowMilliseconds();
    const uint64_t copycatTime = lastCopycatAt.load();
    const bool copycatFresh = needCopycat && copycatTime && now - copycatTime <= 5000 &&
        !currentCopycatPlayers.empty();
    if (copycatFresh) {
        std::string currentSelfUid;
        {
            std::lock_guard<std::mutex> lock(boneMutex);
            currentSelfUid = selfUid;
        }
        for (const CopycatPlayer &player : currentCopycatPlayers) {
            if (currentSelfUid != "unknown" && player.uid == currentSelfUid) continue;
            const float playerDistance = distanceToPlayer(player.position);
            if (std::isfinite(playerDistance) && playerDistance < 1.0f) continue;

            BonePoint body = player.position;
            BonePoint head = player.position;
            body.y += 8.5f;
            head.y += 28.5f;
            ImVec2 bodyScreen;
            ImVec2 headScreen;
            if (!ProjectPoint(body, viewProjection, centerX, centerY, bodyScreen) ||
                !ProjectPoint(head, viewProjection, centerX, centerY, headScreen)) continue;
            const float height = bodyScreen.y - headScreen.y;
            if (height <= 2.0f) continue;
            const float width = height * 0.5f;
            const ImVec2 minimum(bodyScreen.x - width * 0.5f, headScreen.y);
            const ImVec2 maximum(bodyScreen.x + width * 0.5f, bodyScreen.y);
            const int roleId = player.identityId > 0 ? player.identityId : player.roleId;
            const int campId = CopycatCampId(player);
            const ImU32 color = CopycatCampColor(campId);
            drawList->AddRectFilled(minimum, maximum, IM_COL32(0, 0, 0, 32), 3.0f);
            drawList->AddRect(minimum, maximum, color, 3.0f, 0, 2.2f);

            char label[160];
            if (player.index > 0) {
                std::snprintf(label, sizeof(label), "[%d号] %s | %s", player.index,
                              CopycatRoleName(roleId), CopycatCampName(campId));
            } else {
                std::snprintf(label, sizeof(label), "%s | %s",
                              CopycatRoleName(roleId), CopycatCampName(campId));
            }
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2(bodyScreen.x - textSize.x * 0.5f, headScreen.y - textSize.y - 5.0f), color, label);
            if (std::isfinite(playerDistance)) {
                char distanceLabel[32];
                std::snprintf(distanceLabel, sizeof(distanceLabel), "%.0f 米", playerDistance);
                const ImVec2 distanceSize = ImGui::CalcTextSize(distanceLabel);
                drawList->AddText(ImVec2(bodyScreen.x - distanceSize.x * 0.5f, bodyScreen.y + 5.0f), color, distanceLabel);
            }
        }
    }

    if (drawFallbackBoxes && !copycatFresh && kernelPlayerCount == 0 &&
        lastActionAt.load() && now - lastActionAt.load() <= 1500) {
        std::string currentSelfUid;
        {
            std::lock_guard<std::mutex> lock(boneMutex);
            currentSelfUid = selfUid;
        }
        for (const ActionPlayer &player : currentPlayers) {
            if (player.uid == currentSelfUid) continue;
            BonePoint body = player.position;
            BonePoint head = player.position;
            body.y += 8.5f;
            head.y += 28.5f;
            ImVec2 bodyScreen;
            ImVec2 headScreen;
            if (!ProjectPoint(body, viewProjection, centerX, centerY, bodyScreen) ||
                !ProjectPoint(head, viewProjection, centerX, centerY, headScreen)) continue;
            const float height = bodyScreen.y - headScreen.y;
            if (height <= 2.0f) continue;
            const float width = height * 0.5f;
            const ImVec2 minimum(bodyScreen.x - width * 0.5f, headScreen.y);
            const ImVec2 maximum(bodyScreen.x + width * 0.5f, bodyScreen.y);
            const bool hunter = player.unitType == 1;
            const ImU32 color = hunter ? IM_COL32(255, 60, 60, 235) : IM_COL32(40, 255, 90, 235);
            drawList->AddRect(minimum, maximum, color, 3.0f, 0, 2.0f);
            char label[128];
            std::snprintf(label, sizeof(label), "[%s] UID:%s", hunter ? "监管者" : "求生者", player.uid.c_str());
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2(bodyScreen.x - textSize.x * 0.5f, headScreen.y - textSize.y - 5.0f), color, label);
        }
    }

    if (lastInfoAt.load() && now - lastInfoAt.load() <= 2500) {
        auto drawProgressObjects = [&](const std::vector<ProgressObject> &objects, const char *name,
                                       ImU32 baseColor, bool showProgress, float maxDistance,
                                       bool progressBypassesDistance, bool hideCompleted) {
            for (const ProgressObject &object : objects) {
                if (hideCompleted && object.progress >= 99.9f) continue;
                if (maxDistance > 0.0f && distanceToPlayer(object.position) > maxDistance &&
                    (!progressBypassesDistance || object.progress <= 10.0f)) continue;
                ImVec2 screen;
                if (!ProjectPoint(object.position, viewProjection, centerX, centerY, screen)) continue;
                char label[96];
                if (showProgress) std::snprintf(label, sizeof(label), "[%s%d] %.1f%%", name, object.index, object.progress);
                else std::snprintf(label, sizeof(label), "[%s%d]", name, object.index);
                const ImU32 color = showProgress && object.progress >= 99.9f ? IM_COL32(40, 255, 90, 240) : baseColor;
                const ImVec2 textSize = ImGui::CalcTextSize(label);
                drawList->AddCircleFilled(screen, 4.0f, color);
                drawList->AddText(ImVec2(screen.x - textSize.x * 0.5f, screen.y - textSize.y - 8.0f), color, label);
            }
        };
        if (drawGenerators) drawProgressObjects(currentGenerators, "密码机", IM_COL32(255, 210, 30, 240), true, 40.0f, true, true);
        if (drawExitGates) drawProgressObjects(currentGates, "大门", IM_COL32(210, 90, 255, 240), true, 0.0f, false, false);
        if (drawBasements) drawProgressObjects(currentBasements, "地窖", IM_COL32(50, 220, 255, 240), false, 0.0f, false, false);
    }

    if (lastPanelAt.load() && now - lastPanelAt.load() <= 2500) {
        auto drawPanelObjects = [&](const std::vector<PanelObject> &objects, const char *name,
                                    ImU32 color, int kind, float maxDistance) {
            for (const PanelObject &object : objects) {
                if (maxDistance > 0.0f && distanceToPlayer(object.position) > maxDistance) continue;
                ImVec2 screen;
                if (!ProjectPoint(object.position, viewProjection, centerX, centerY, screen)) continue;
                char label[96];
                if (kind == 0) {
                    std::snprintf(label, sizeof(label), "[%s %s]", name,
                                  object.spanning ? "交互中" : (object.usable ? "可用" : "已使用"));
                } else if (kind == 1 && object.progress >= 0.0f) {
                    std::snprintf(label, sizeof(label), "[%s] %.1f%%", name, object.progress);
                } else {
                    std::snprintf(label, sizeof(label), "[%s]", name);
                }
                const ImVec2 textSize = ImGui::CalcTextSize(label);
                drawList->AddCircleFilled(screen, 3.5f, color);
                drawList->AddText(ImVec2(screen.x - textSize.x * 0.5f, screen.y - textSize.y - 7.0f), color, label);
            }
        };
        if (drawPanels) drawPanelObjects(currentPanels, "木板", IM_COL32(255, 150, 40, 230), 0, 10.0f);
        if (drawChairs) drawPanelObjects(currentChairs, "椅子", IM_COL32(255, 70, 120, 230), 1, 15.0f);
        if (drawWindows) drawPanelObjects(currentWindows, "窗户", IM_COL32(80, 190, 255, 220), 2, 0.0f);
    }

    if (!drawBones) return;
    const uint64_t boneTime = lastBoneAt.load();
    if (!boneTime || now - boneTime > 1500) return;

    std::vector<BoneUnit> units;
    std::string currentSelfUid;
    {
        std::lock_guard<std::mutex> lock(boneMutex);
        units = boneUnits;
        currentSelfUid = selfUid;
    }
    static const char *connections[][2] = {
        {"biped head", "biped neck"},
        {"biped neck", "biped spine"},
        {"biped neck", "biped l clavicle"},
        {"biped l clavicle", "biped l upperarm"},
        {"biped l upperarm", "biped l forearm"},
        {"biped l forearm", "biped l hand"},
        {"biped neck", "biped r clavicle"},
        {"biped r clavicle", "biped r upperarm"},
        {"biped r upperarm", "biped r forearm"},
        {"biped r forearm", "biped r hand"},
        {"biped spine", "biped l thigh"},
        {"biped l thigh", "biped l calf"},
        {"biped l calf", "biped l foot"},
        {"biped spine", "biped r thigh"},
        {"biped r thigh", "biped r calf"},
        {"biped r calf", "biped r foot"}
    };

    for (const BoneUnit &unit : units) {
        if (ignoreSelfBones && unit.uid == currentSelfUid) continue;
        for (const auto &connection : connections) {
            const auto first = unit.points.find(connection[0]);
            const auto second = unit.points.find(connection[1]);
            if (first == unit.points.end() || second == unit.points.end()) continue;
            ImVec2 firstScreen;
            ImVec2 secondScreen;
            if (!ProjectPoint(first->second, viewProjection, centerX, centerY, firstScreen) ||
                !ProjectPoint(second->second, viewProjection, centerX, centerY, secondScreen)) continue;
            const ImU32 color = first->second.visible && second->second.visible
                ? IM_COL32(40, 255, 90, 230) : IM_COL32(255, 70, 70, 230);
            drawList->AddLine(firstScreen, secondScreen, color, 2.2f);
        }
        if (drawBoneUid) {
            const auto head = unit.points.find("biped head");
            ImVec2 headScreen;
            if (head != unit.points.end() && ProjectPoint(head->second, viewProjection, centerX, centerY, headScreen)) {
                drawList->AddText(ImVec2(headScreen.x + 6.0f, headScreen.y - 12.0f), IM_COL32(255, 255, 255, 230), unit.uid.c_str());
            }
        }
    }
}

}
