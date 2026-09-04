CC = gcc
WINDRES = windres
CFLAGS = -O2

ARCH := $(shell $(CC) -dumpmachine)
ifneq (,$(findstring x86_64,$(ARCH)))
  PTHREAD_DEFSYM = pthread_once=pthread_once
else
  PTHREAD_DEFSYM = pthread_once=_pthread_once
endif

LDFLAGS = -lgdi32 -lcomdlg32 -no-pthread -Wl,-subsystem,windows -Wl,--defsym,$(PTHREAD_DEFSYM) -l:libwinpthread.a
TARGET = picview-gcc.exe

OBJS = picview.c picview.res.o

all: $(TARGET)

picview.res.o: picview.rc
	$(WINDRES) $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	-del /f /q $(TARGET) picview.res.o 2>nul

.PHONY: all clean
