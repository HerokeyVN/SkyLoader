TARGET := ../../dist/SkyBootstrap.dll
SRC := src/dllmain.cpp

CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -municode -DSKYBOOTSTRAP_BUILD -Iinclude
LDFLAGS := -shared -static -static-libgcc -static-libstdc++ -Wl,--gc-sections

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) | ../../dist

	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

../../dist:

	mkdir ../../dist

clean:

	-del /q ..\..\dist\SkyBootstrap.dll
