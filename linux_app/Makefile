SRC_DIR = src
INC_DIR = include
BLD_DIR = build
LIB_DIR = ../libs

TARGET_NAME = wifi_hp_app

CJSON_DIR = $(LIB_DIR)/cjson
CJSON_BLD_DIR = $(BLD_DIR)/cjson_build
CJSON_LIB = $(CJSON_BLD_DIR)/libcjson.a

OBJ_DIR = $(BLD_DIR)/obj
CC = gcc
CFLAGS = -Wall -g `pkg-config --cflags libpulse` -I$(INC_DIR) -I$(CJSON_DIR)
CFLAGS += -DCONFIG_DAEMON_NAME="\"$(TARGET_NAME)\""
LDFLAGS = `pkg-config --libs libpulse` -lpthread $(CJSON_LIB)

SRC = $(wildcard $(SRC_DIR)/*.c) $(LIB_DIR)/cjson/cJSON.c
OBJ = $(SRC:%.c=$(OBJ_DIR)/%.o) 

TARGET = $(BLD_DIR)/$(TARGET_NAME)

.PHONY: all clean_obj clean

all: $(CJSON_LIB) clean_obj $(TARGET)

$(CJSON_LIB):
	@echo "Building cJSON with CMake..."
	@mkdir -p $(CJSON_BLD_DIR)
	@cd $(CJSON_BLD_DIR) && \
	 cmake $(abspath $(CJSON_DIR)) \
		 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
		 -DCMAKE_BUILD_TYPE=Release \
		 -DBUILD_SHARED_LIBS=OFF \
		 -DENABLE_CJSON_UTILS=OFF \
		 -DENABLE_CJSON_TEST=OFF \
		 -DCMAKE_POSITION_INDEPENDENT_CODE=ON && \
	 $(MAKE)
	@echo "cJSON built successfully"

$(TARGET): $(OBJ)
	@mkdir -p $(@D)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -o $@ -c $< $(CFLAGS)

clean_obj:
	@echo "Cleaning obj files"
	@rm -rf $(OBJ_DIR)

clean:
	@rm -rf $(BLD_DIR)
