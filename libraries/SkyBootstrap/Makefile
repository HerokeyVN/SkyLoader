TARGET := ../../dist/SkyBootstrap.dll
SRC := src/dllmain.cpp src/vulkan_layer.cpp

CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -municode -DSKYBOOTSTRAP_BUILD -Iinclude -I../../../SkyToolkit/libraries/html-sky-reference/libraries/vulkan/Include
LDFLAGS := -shared -static -static-libgcc -static-libstdc++ -Wl,--gc-sections C:/Windows/System32/vulkan-1.dll

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) | ../../dist

	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

../../dist:

	mkdir ../../dist

clean:

	-del /q ..\..\dist\SkyBootstrap.dll
