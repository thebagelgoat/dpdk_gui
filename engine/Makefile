CC      := gcc
CFLAGS  := -O2 -march=native -Wall -Wextra -Wno-unused-parameter \
           $(shell pkg-config --cflags libdpdk)
LDFLAGS := $(shell pkg-config --libs libdpdk) \
           -ljansson -lpthread -lnuma -lm

SRCS := main.c \
        graph.c \
        node_out.c \
        worker.c \
        stats.c \
        ipc.c \
        modules/nic_rx.c \
        modules/nic_tx.c \
        modules/ip_filter.c \
        modules/vlan_filter.c \
        modules/port_filter.c \
        modules/protocol_filter.c \
        modules/mac_filter.c \
        modules/pcap_recorder.c \
        modules/counter.c \
        modules/template.c \
        modules/pcap_source.c \
        modules/pkt_gen.c \
        modules/packet_inspector.c

OBJS    := $(SRCS:.c=.o)
TARGET  := dpdk_engine

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
