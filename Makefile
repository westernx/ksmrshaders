
VERSION := 2014

MI_SRC := $(wildcard *.mi)
MI_DST := $(MI_SRC:%=${VERSION}/%)
CPPS := $(MI_SRC:%.mi=%.cpp)

LIB_EXT := .so
UNAME_S := $(shell uname -s)
ifeq (${UNAME_S},Darwin)
	LIB_EXT := .dylib
endif

LIBS := $(CPPS:%.cpp=${VERSION}/%${LIB_EXT})


.PHONY: default clean

default: ${LIBS} ${MI_DST}

${VERSION}/%.dylib: %.cpp
	@ mkdir -p $(dir $@)
	g++ -shared \
		-undefined dynamic_lookup \
		-I/Applications/Autodesk/mentalrayForMaya${VERSION}/devkit/adskShaderSDK/include \
		-I/Applications/Autodesk/mentalrayForMaya${VERSION}/devkit/include \
		-o $@ $<

${VERSION}/%.mi: %.mi
	cp $< $@

clean:
	- rm ${LIBS}
