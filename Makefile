TARGET := dist/SkyLoader.exe
SRC := src/main.cpp src/loader_controller.cpp
RESOURCE_SCRIPT := resources/skyloader.rc
RESOURCE_OBJECT := dist/skyloader.res
BOOTSTRAP_DIR := libraries/SkyBootstrap

CXX := g++
WINDRES := windres
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -municode
LDFLAGS := -mwindows -static -static-libgcc -static-libstdc++ -lcomctl32 -lshell32

.PHONY: all clean bootstrap

all: $(TARGET) bootstrap

bootstrap:

	$(MAKE) -C $(BOOTSTRAP_DIR) all

$(TARGET): $(SRC) $(RESOURCE_OBJECT) | dist

	$(CXX) $(CXXFLAGS) $(SRC) $(RESOURCE_OBJECT) -o $@ $(LDFLAGS)

$(RESOURCE_OBJECT): $(RESOURCE_SCRIPT) resources/skyloader.ico | dist

	$(WINDRES) -i $(RESOURCE_SCRIPT) -O coff -o $@

dist:

	mkdir dist

clean:

	-del /q dist\SkyLoader.exe
	-del /q dist\skyloader.res
	-del /q dist\SkyBootstrap-vulkan-layer.json
	-$(MAKE) -C $(BOOTSTRAP_DIR) clean
