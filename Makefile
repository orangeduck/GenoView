PLATFORM ?= PLATFORM_DESKTOP
BUILD_MODE ?= DEBUG
DEFINES = -D _DEFAULT_SOURCE -D RAYLIB_BUILD_MODE=$(BUILD_MODE) -D $(PLATFORM)
PLATFORM_OS ?= $(shell uname)

ifeq ($(PLATFORM),PLATFORM_DESKTOP)
    
    CC = gcc
    
    ifeq ($(findstring MINGW,$(PLATFORM_OS)),MINGW)
        EXT = .exe
        RAYLIB_DIR = C:/raylib
        INCLUDE_DIR = -I ./ -I $(RAYLIB_DIR)/raylib/src -I $(RAYLIB_DIR)/raygui/src
        LIBRARY_DIR = -L $(RAYLIB_DIR)/raylib/src
        ifeq ($(BUILD_MODE),RELEASE)
            CFLAGS ?= $(DEFINES) -Wall -mwindows -D NDEBUG -O3 $(INCLUDE_DIR) $(LIBRARY_DIR) 
        else
            CFLAGS ?= $(DEFINES) -Wall -g $(INCLUDE_DIR) $(LIBRARY_DIR)
        endif
        LIBS = -lraylib -lopengl32 -lgdi32 -lwinmm
    endif

endif

.PHONY: all

all: genoview

genoview: genoview.c
	$(CC) -o $@$(EXT) genoview.c $(CFLAGS) $(LIBS) 

clean:
	rm genoview$(EXT)
