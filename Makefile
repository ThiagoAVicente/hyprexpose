PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner

CXXFLAGS := -std=c++17 -Wall -Wextra -O2 $(shell $(PKG_CONFIG) --cflags wayland-client cairo pangocairo)
LDFLAGS  := $(shell $(PKG_CONFIG) --libs wayland-client cairo pangocairo) -lrt

PROTO_DIR  := protocols
BUILD_DIR  := build
SRC_DIR    := src

PROTOS     := wlr-layer-shell-unstable-v1 hyprland-toplevel-export-v1
PROTO_SRCS := $(addprefix $(BUILD_DIR)/,$(addsuffix -protocol.c,$(PROTOS)))
PROTO_HDRS := $(addprefix $(BUILD_DIR)/,$(addsuffix -client-protocol.h,$(PROTOS)))
PROTO_OBJS := $(PROTO_SRCS:.c=.o)

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
C_SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
C_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))

TARGET := hyprexpose

all: $(TARGET)

$(TARGET): $(OBJS) $(C_OBJS) $(PROTO_OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 $(shell $(PKG_CONFIG) --cflags wayland-client) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(PROTO_HDRS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(BUILD_DIR) -c $< -o $@

$(BUILD_DIR)/%-protocol.o: $(BUILD_DIR)/%-protocol.c $(PROTO_HDRS) | $(BUILD_DIR)
	$(CC) -std=c11 -O2 $(shell $(PKG_CONFIG) --cflags wayland-client) -c $< -o $@

$(BUILD_DIR)/%-protocol.c: $(PROTO_DIR)/%.xml | $(BUILD_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(BUILD_DIR)/%-client-protocol.h: $(PROTO_DIR)/%.xml | $(BUILD_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
