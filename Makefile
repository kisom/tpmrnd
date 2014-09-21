TARGET = tpmrnd

CFLAGS := $(CFLAGS) -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align
CFLAGS += -Wwrite-strings -Wmissing-prototypes -Wmissing-declarations
CFLAGS += -Wredundant-decls -Wnested-externs -Winline -Wno-long-long
CFLAGS += -Wconversion -Wstrict-prototypes -Wall -Werror -std=c99
CFLAGS += -D_BSD_SOURCE -D_XOPEN_SOURCE

all: $(TARGET)

$(TARGET): tpmrnd.c
	$(CC) $(CFLAGS) -o $@ tpmrnd.c -ltspi

clean:
	rm -f *.o $(TARGET) *core*
