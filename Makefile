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
SRC       := main.c sip_server.c network_utils.c
OBJ       := $(addprefix $(OBJDIR)/,$(SRC:.c=.o))
TARGET    := $(BINDIR)/$(PROJECT)

# Includes
CPPFLAGS  += -I. -MMD -MP
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

.PHONY: all run clean distclean rebuild debug release

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(BINDIR)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Convenience targets
run: $(TARGET)
	@$(TARGET)

rebuild: distclean all

debug:
	@$(MAKE) DEBUG=1 SAN=0 clean all

release:
	@$(MAKE) DEBUG=0 SAN=0 clean all

clean:
	@rm -rf $(OBJDIR)/*.o $(OBJDIR)/*.d $(TARGET)

distclean:
	@rm -rf build $(PROJECT) *.o *.d

# Auto-include dependency files
-include $(OBJ:.o=.d)
