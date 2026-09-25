#ifndef IDV_KERNEL_DRIVER_QXQD_HPP
#define IDV_KERNEL_DRIVER_QXQD_HPP

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define OP_CMD_READ 601
#define OP_CMD_WRITE 602
#define OP_CMD_BASE 603
#define OP_CMD_GETPID 604
#define OP_CMD_HIDE_PROCESS 605
#define OP_CMD_UN_HOOK 606
#define OP_CMD_RECOVER_PROCESS 607
#define OP_CMD_GYRO 608

#define PARADISE_GYRO_MASK_GYRO (1u << 0)
#define PARADISE_GYRO_MASK_UNCAL (1u << 1)
#define PARADISE_GYRO_MASK_ALL (PARADISE_GYRO_MASK_GYRO | PARADISE_GYRO_MASK_UNCAL)

struct paradise_gyro_config_cmd {
    int enable;
    uint32_t type_mask;
    float x;
    float y;
    float z;
};

typedef struct _COPY_MEMORY {
    pid_t pid;
    uintptr_t addr;
    void *buffer;
    size_t size;
} COPY_MEMORY, *PCOPY_MEMORY;

typedef struct _MODULE_BASE {
    pid_t pid;
    char *name;
    uintptr_t base;
    short index;
} MODULE_BASE, *PMODULE_BASE;

class c_driver {
private:
    int fd;
    pid_t pid;

    bool probe_kernel() {
        const uint32_t expected = 0x4B524E4C;
        uint32_t actual = 0;
        COPY_MEMORY command{};
        command.pid = getpid();
        command.addr = reinterpret_cast<uintptr_t>(&expected);
        command.buffer = &actual;
        command.size = sizeof(actual);
        return ioctl(fd, OP_CMD_READ, &command) == 0 && actual == expected;
    }

public:
    c_driver() : fd(socket(AF_INET, SOCK_STREAM, 0)), pid(-1) {
        if (fd < 0 || !probe_kernel()) {
            std::printf("[-] 内核获取失败，程序结束\n");
            std::fflush(stdout);
            if (fd >= 0) {
                close(fd);
            }
            std::exit(EXIT_FAILURE);
        }
    }

    ~c_driver() {
        if (fd >= 0) {
            close(fd);
        }
    }

    bool ready() const {
        return fd >= 0;
    }

    void initialize(pid_t target_pid) {
        pid = target_pid;
    }

    bool read(uintptr_t addr, void *buffer, size_t size) {
        if (!ready() || pid <= 0 || buffer == nullptr || size == 0) {
            return false;
        }

        COPY_MEMORY command{};
        command.pid = pid;
        command.addr = addr;
        command.buffer = buffer;
        command.size = size;
        return ioctl(fd, OP_CMD_READ, &command) == 0;
    }

    bool write(uintptr_t addr, const void *buffer, size_t size) {
        if (!ready() || pid <= 0 || buffer == nullptr || size == 0) {
            return false;
        }

        COPY_MEMORY command{};
        command.pid = pid;
        command.addr = addr;
        command.buffer = const_cast<void *>(buffer);
        command.size = size;
        return ioctl(fd, OP_CMD_WRITE, &command) == 0;
    }

    template <typename T>
    T read(uintptr_t addr) {
        T result{};
        read(addr, &result, sizeof(T));
        return result;
    }

    template <typename T>
    bool write(uintptr_t addr, const T &value) {
        return write(addr, &value, sizeof(T));
    }

    uintptr_t get_module_base(const char *module_name, short index = 0) {
        if (!ready() || pid <= 0 || module_name == nullptr) {
            return 0;
        }

        MODULE_BASE command{};
        command.pid = pid;
        command.name = const_cast<char *>(module_name);
        command.index = index;
        if (ioctl(fd, OP_CMD_BASE, &command) != 0) {
            return 0;
        }
        return command.base;
    }

    bool hide_process() {
        return ready() && ioctl(fd, OP_CMD_HIDE_PROCESS) == 0;
    }

    bool recover_process() {
        return ready() && ioctl(fd, OP_CMD_RECOVER_PROCESS) == 0;
    }

    bool un_hook() {
        return ready() && ioctl(fd, OP_CMD_UN_HOOK) == 0;
    }

    bool gyro_update(bool enable, float x, float y,
                     uint32_t type_mask = PARADISE_GYRO_MASK_ALL) {
        if (!ready()) {
            return false;
        }

        paradise_gyro_config_cmd command{};
        command.enable = enable ? 1 : 0;
        command.type_mask = type_mask;
        command.x = x;
        command.y = y;
        return ioctl(fd, OP_CMD_GYRO, &command) == 0;
    }
};

static c_driver *driver = new c_driver();
static pid_t pid = -1;

inline int getPID(char *package_name) {
    if (package_name == nullptr || package_name[0] == '\0') {
        return -1;
    }

    char command[256];
    if (std::snprintf(command, sizeof(command), "pidof %s", package_name) >=
        static_cast<int>(sizeof(command))) {
        return -1;
    }

    FILE *process = popen(command, "r");
    if (process == nullptr) {
        return -1;
    }

    pid_t target_pid = -1;
    if (std::fscanf(process, "%d", &target_pid) != 1) {
        target_pid = -1;
    }
    pclose(process);

    if (target_pid > 0) {
        pid = target_pid;
        driver->initialize(target_pid);
    }
    return target_pid;
}

inline int get_name_pid(char *package_name) {
    return getPID(package_name);
}

inline bool PidExamIne() {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d", pid);
    return pid > 0 && access(path, F_OK) == 0;
}

inline long getModuleBase(char *module_name) {
    return static_cast<long>(driver->get_module_base(module_name));
}

inline bool vm_readv(unsigned long address, void *buffer, size_t size) {
    return driver->read(address, buffer, size);
}

inline bool vm_writev(unsigned long address, const void *buffer, size_t size) {
    return driver->write(address, buffer, size);
}

inline float getfloat(unsigned long address) {
    return driver->read<float>(address);
}

inline float getFloat(unsigned long address) {
    return driver->read<float>(address);
}

inline int getdword(unsigned long address) {
    return driver->read<int>(address);
}

inline int getDword(unsigned long address) {
    return driver->read<int>(address);
}

inline unsigned int getPtr32(unsigned int address) {
    return driver->read<unsigned int>(address);
}

inline unsigned long getPtr64(unsigned long address) {
    return driver->read<unsigned long>(address) & 0x00FFFFFFFFFFFFFFUL;
}

inline void writedword(unsigned long address, int data) {
    driver->write(address, data);
}

inline void writefloat(unsigned long address, float data) {
    driver->write(address, data);
}

#endif
