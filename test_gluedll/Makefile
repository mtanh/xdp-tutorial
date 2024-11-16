# Directory
COMMON_DIR=../common
OBJECT_DIR=./obj
BIN_DIR=./bin

# File
SOURCE_FILES=
OBJECT_FILES=$(OBJECT_DIR)/glue_dll.o $(OBJECT_DIR)/prog.o

# Compiler
CC=gcc
CFLAGS=-I$(COMMON_DIR)
C_WARNINGS=-Wall -Wextra -Wpedantic
LDFLAGS=
C_COMPILER_CALL = $(CC) $(C_WARNINGS) $(CFLAGS) 

# Target
EXECUTABLE_NAME=prog

# Command
create:
	@mkdir -p $(OBJECT_DIR) $(BIN_DIR)

all: create $(OBJECT_FILES)
	$(C_COMPILER_CALL) $(OBJECT_FILES) $(LDFLAGS) -o $(BIN_DIR)/$(EXECUTABLE_NAME)

$(OBJECT_DIR)/glue_dll.o: $(COMMON_DIR)/glue_dll.c $(COMMON_DIR)/glue_dll.h
	$(C_COMPILER_CALL) -c -o $@ $<

$(OBJECT_DIR)/prog.o: prog.c
	$(C_COMPILER_CALL) -c -o $@ $<

run:
	./$(BIN_DIR)/$(EXECUTABLE_NAME)  > log.1 2> log.2

.PHONY: clean run

clean:
	rm -fr $(OBJECT_DIR)/*.*
	rm -fr $(BIN_DIR)/$(EXECUTABLE_NAME)
