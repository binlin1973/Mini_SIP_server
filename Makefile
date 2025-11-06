# Project
PROJECT   ?= sip_server

# Compiler & flags
CC        ?= gcc
CSTD      ?= c11
WARN      ?= -Wall -Wextra -Wpedantic
OPT       ?= -O2
DEBUG     ?= 1
# 1: debug (-g -O0)  0: release ($(OPT))
SAN       ?= 0            # 1: enable ASan/UBSan

# Dirs
SRCDIR    := .
OBJDIR    := build/obj
BINDIR    := build/bin

# Sources / Objects / Target
SRC        := main.c sip_server.c network_utils.c
OBJ        := $(addprefix $(OBJDIR)/,$(SRC:.c=.o))
TARGET     := $(BINDIR)/$(PROJECT)

TESTDIR    := tests
TESTBINDIR := build/tests
TESTS      := test_parsing test_state_machine test_register test_integration_flow
TESTFLAGS += -DTESTING
TEST_SOURCES := $(addprefix $(TESTDIR)/,$(TESTS:=.c))
TEST_BINS    := $(addprefix $(TESTBINDIR)/,$(TESTS))

# Includes
CPPFLAGS  += -I. -I$(TESTDIR) -MMD -MP
# Compile flags
CFLAGS    += -std=$(CSTD) $(WARN) -pthread
# Link flags
LDFLAGS   += -pthread
LDLIBS    +=

# Build type
ifeq ($(DEBUG),1)
  CFLAGS  += -g -O0
else
  CFLAGS  += $(OPT)
endif

# Sanitizers
ifeq ($(SAN),1)
  CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
  LDFLAGS += -fsanitize=address,undefined
endif

.PHONY: all run clean distclean rebuild debug release test

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(BINDIR)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TESTBINDIR)/%: $(TESTDIR)/%.c $(TESTDIR)/mocks.c $(TESTDIR)/mocks.h $(TESTDIR)/test_common.h
	@mkdir -p $(TESTBINDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TESTFLAGS) $< $(TESTDIR)/mocks.c -o $@ $(LDFLAGS) $(LDLIBS)

# Convenience targets
run: $(TARGET)
	@$(TARGET)

test: $(TEST_BINS)
	@total=0; passed=0; failed=0; \
	for t in $(TEST_BINS); do \
	  printf "Running %s...\n" "$$t"; \
	  total=$$((total+1)); \
	  if "$$t"; then \
	    passed=$$((passed+1)); \
	  else \
	    failed=$$((failed+1)); \
	  fi; \
	done; \
	if [ $$total -eq 0 ]; then \
	  echo "No tests found."; \
	else \
	  echo ""; \
	  echo "$$passed/$$total tests passed"; \
	  echo "Failures: $$failed"; \
	fi; \
	[ $$failed -eq 0 ]

rebuild: distclean all

debug:
	@$(MAKE) DEBUG=1 SAN=0 clean all

release:
	@$(MAKE) DEBUG=0 SAN=0 clean all

clean:
	@rm -rf $(OBJDIR) $(TARGET) $(TESTBINDIR)

distclean:
	@rm -rf build $(PROJECT) *.o *.d

# Auto-include dependency files
-include $(OBJ:.o=.d)
