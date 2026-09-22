TARGET := dist/SkyLoader.exe
SRC := src/main.cpp src/loader_controller.cpp
BOOTSTRAP_DIR := libraries/SkyBootstrap

CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -municode
LDFLAGS := -mwindows -static -static-libgcc -static-libstdc++ -lcomctl32 -lshell32

.PHONY: all clean bootstrap

all: $(TARGET) bootstrap

bootstrap:

	$(MAKE) -C $(BOOTSTRAP_DIR) all

$(TARGET): $(SRC) | dist

	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

dist:

	mkdir dist

clean:

	-del /q dist\SkyLoader.exe
	-del /q dist\SkyBootstrap-vulkan-layer.json
	-$(MAKE) -C $(BOOTSTRAP_DIR) clean
