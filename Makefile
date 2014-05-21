MIS := $(wildcard *.mi)
CPPS := $(MIS:%.mi=%.cpp)

LIB_EXT := .so
UNAME_S := $(shell uname -s)
ifeq (${UNAME_S},Darwin)
	LIB_EXT := .dylib
endif

LIBS := $(CPPS:%.cpp=%${LIB_EXT})


.PHONY: default clean

default: ${LIBS}

%.dylib: %.cpp
	g++ -shared \
		-undefined dynamic_lookup \
		-I/Applications/Autodesk/mentalrayForMaya2014/devkit/adskShaderSDK/include \
		-I/Applications/Autodesk/mentalrayForMaya2014/devkit/include \
		-o $@ $<

clean:
	- rm *${LIB_EXT}
