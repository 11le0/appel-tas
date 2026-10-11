CC      ?= cc
CFLAGS  ?= -O3 -march=native -flto -Wall -Wextra -std=c11 -pthread
LDFLAGS ?= -flto -lm -pthread

SRCS = main.c physics.c level.c playerstate_json.c hashset.c expr.c spikes.c
OBJS = $(SRCS:.c=.o)

TARGET = bruteforce

all: $(TARGET) randy-bin

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

portable:
	$(MAKE) CFLAGS="-O3 -flto -Wall -Wextra -std=c11 -pthread" $(TARGET)

randy-bin:
	$(MAKE) -C randy randy

clean:
	rm -f $(OBJS) $(TARGET)
	$(MAKE) -C randy clean

.PHONY: all clean portable randy-bin
