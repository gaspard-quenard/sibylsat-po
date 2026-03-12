# Makefile for building sibylsat as an IPASIR application
# (see github.com/biotomas/ipasir )

TARGET=$(shell basename "`pwd`")
IPASIRSOLVER ?= glucose4
CMAKE_BUILD_TYPE ?= RELEASE

all: clean
	mkdir -p build
	cd build && cmake .. -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DIPASIRSOLVER=$(IPASIRSOLVER) && make -j && cp sibylsat-po .. && cd ..

clean:
	rm -rf $(TARGET) build/
