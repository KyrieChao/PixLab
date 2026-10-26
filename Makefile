CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -g
LDLIBS  = -lm

SRCDIR  = src
SOURCES = $(SRCDIR)/main.c \
          $(SRCDIR)/cli.c \
          $(SRCDIR)/image.c \
          $(SRCDIR)/bmp.c \
          $(SRCDIR)/filter.c \
          $(SRCDIR)/util.c

TARGET  = pixlab.exe
SAMPLES = samples

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

# ---- 测试程序 ----

tests/test_bmp.exe: tests/test_bmp.c $(SRCDIR)/bmp.c $(SRCDIR)/image.c $(SRCDIR)/util.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $^ $(LDLIBS)

tests/make_exotic.exe: tests/make_exotic.c
	$(CC) $(CFLAGS) -o $@ $<

# ---- 测试样本 ----
# A 类：Windows 自带的 System.Drawing 生成全位深样本 + 24 位参照
samples: $(TARGET)
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/gen_samples.ps1

# B 类：自己按字节构造 RLE / CORE 头 / V4-V5 / 自顶向下，再让 System.Drawing
# 读回一份 24 位参照作为独立基准
exotic: tests/make_exotic.exe
	mkdir -p $(SAMPLES)/exotic
	./tests/make_exotic.exe $(SAMPLES)/exotic
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/gen_refs.ps1

# ---- 回归 ----
test: $(TARGET) tests/test_bmp.exe samples exotic
	bash tests/run_tests.sh

clean:
	rm -f $(TARGET) tests/*.exe $(SAMPLES)/*.bmp $(SAMPLES)/exotic/*.bmp

.PHONY: all clean samples exotic test
