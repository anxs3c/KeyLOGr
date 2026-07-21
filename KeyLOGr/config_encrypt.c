#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void xor_encrypt(const char *input, const char *output, const char *key) {
    int fd_in = open(input, O_RDONLY);
    if (fd_in < 0) return;
    
    int fd_out = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out < 0) {
        close(fd_in);
        return;
    }
    
    unsigned char buffer[4096];
    ssize_t bytes;
    size_t key_len = strlen(key);
    size_t key_pos = 0;
    
    while ((bytes = read(fd_in, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes; i++) {
            buffer[i] ^= key[key_pos++ % key_len];
        }
        write(fd_out, buffer, bytes);
    }
    
    close(fd_in);
    close(fd_out);
}

void generate_config() {
    const char *config_template = 
        "c2.example.com\n"
        "443\n"
        "1\n"
        "2\n"
        "300\n";
    
    int fd = open("config.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    write(fd, config_template, strlen(config_template));
    close(fd);
    
    xor_encrypt("config.txt", "config.enc", "REDTEAM2026");
    unlink("config.txt");
}

int main() {
    generate_config();
    return 0;
}