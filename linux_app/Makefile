SRC_DIR = src
INC_DIR = include
BLD_DIR = build

OBJ_DIR = $(BLD_DIR)/obj
CC = gcc
CFLAGS = -Wall -g `pkg-config --cflags libpulse` -I$(INC_DIR)
LDFLAGS = `pkg-config --libs libpulse` -lpthread

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:%.c=$(OBJ_DIR)/%.o) 

TARGET = $(BLD_DIR)/wifi_hp_app

.PHONY: all clean_obj clean

all: clean_obj $(TARGET)

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
