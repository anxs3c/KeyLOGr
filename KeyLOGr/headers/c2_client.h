#ifndef C2_CLIENT_H
#define C2_CLIENT_H

int init_ssl();
int connect_c2(const char *host, int port, int ssl_enabled);
int send_to_c2(const void *data, size_t len);
int recv_from_c2(void *buffer, size_t buflen);
int check_c2_alive();
void reconnect_c2();
void close_c2_connection();

#endif