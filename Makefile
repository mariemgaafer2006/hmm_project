CC := gcc
AR := ar
ARFLAGS := rcs

SRC_DIR := src
INC_DIR := inc
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
DEP_DIR := $(BUILD_DIR)/dep
LIB_DIR := $(BUILD_DIR)/lib
TEST_DIR := test
EXTRA_DIR := extra_tests

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEPS := $(SRCS:$(SRC_DIR)/%.c=$(DEP_DIR)/%.d)
LIB := $(LIB_DIR)/libhmm.a
SHARED_LIB := $(LIB_DIR)/libhmm.so
TEST_APP := $(BUILD_DIR)/hmm_test

# Objects without the malloc/free wrappers: used by tests that call the
# Hmm* functions directly and keep the C library allocator for printf.
CORE_OBJS := $(filter-out $(OBJ_DIR)/heapWrappers.o,$(OBJS))
STRESS_APP := $(BUILD_DIR)/hmm_stress
SBRK_APP := $(BUILD_DIR)/free_and_sbrk

CPPFLAGS := -I$(INC_DIR)
CFLAGS := -Wall -Wextra -g -fPIC

.PHONY: all static_lib shared_lib test stress free_and_sbrk check clean

all: static_lib test

static_lib: $(LIB)

$(LIB): $(OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

shared_lib: $(SHARED_LIB)

$(SHARED_LIB): $(OBJS) | $(LIB_DIR)
	$(CC) -shared -o $@ $^

test: $(TEST_APP)

$(TEST_APP): $(OBJS) $(wildcard $(TEST_DIR)/*.c) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(wildcard $(TEST_DIR)/*.c) $(OBJS)

# Randomised stress test + edge cases + heap consistency checker.
stress: $(STRESS_APP)

$(STRESS_APP): $(CORE_OBJS) $(EXTRA_DIR)/stress_test.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(EXTRA_DIR)/stress_test.c $(CORE_OBJS)

# Experiment from "The Linux Programming Interface" (free_and_sbrk),
# adapted to print the simulated program break.
free_and_sbrk: $(SBRK_APP)

$(SBRK_APP): $(OBJS) $(EXTRA_DIR)/free_and_sbrk.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(EXTRA_DIR)/free_and_sbrk.c $(OBJS)

# Builds everything and runs every test.
check: test stress free_and_sbrk
	./$(TEST_APP)
	./$(STRESS_APP)
	sh $(EXTRA_DIR)/run_free_and_sbrk.sh

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR) $(DEP_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d -c $< -o $@

$(BUILD_DIR) $(OBJ_DIR) $(DEP_DIR) $(LIB_DIR):
	mkdir -p $@

-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR)
