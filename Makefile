TARGET := dist/SkyLoader.exe
SRC := src/main.cpp src/loader_controller.cpp
RESOURCE_SCRIPT := resources/skyloader.rc
RESOURCE_OBJECT := dist/skyloader.res
BOOTSTRAP_DIR := libraries/SkyBootstrap
OVERLAY_DIR := libraries/SkyOverlay

CXX := g++
WINDRES := windres
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -municode
LDFLAGS := -mwindows -static -static-libgcc -static-libstdc++ -lcomctl32 -lshell32 -lgdi32 -luxtheme -ladvapi32

.PHONY: all clean bootstrap overlay

all: $(TARGET) bootstrap overlay

bootstrap:
	$(MAKE) -C $(BOOTSTRAP_DIR) all

overlay:
	$(MAKE) -C $(OVERLAY_DIR) all

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
	-del /q dist\SkyOverlay.dll
	-$(MAKE) -C $(BOOTSTRAP_DIR) clean
	-$(MAKE) -C $(OVERLAY_DIR) clean
