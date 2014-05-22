
VERSION := 2014

MI_SRC := $(wildcard *.mi)
MI_DST := $(MI_SRC:%=${VERSION}/%)
CPPS := $(MI_SRC:%.mi=%.cpp)

CXXFLAGS :=

LIB_EXT := .so
UNAME_S := $(shell uname -s)

ifeq (${UNAME_S},Darwin)
	LIB_EXT := .dylib
	MR_ROOT := /Applications/Autodesk/mentalrayForMaya${VERSION}
	CXXFLAGS += -undefined dynamic_lookup
else
	MR_ROOT := /opt/autodesk/mentalrayForMaya${VERSION}
	CXXFLAGS += -fPIC
endif

LIBS := $(CPPS:%.cpp=${VERSION}/%${LIB_EXT})


.PHONY: default clean

default: ${LIBS} ${MI_DST}

${VERSION}/%${LIB_EXT}: %.cpp
	@ mkdir -p $(dir $@)
	g++ -shared ${CXXFLAGS} \
		-I${MR_ROOT}/devkit/adskShaderSDK/include \
		-I${MR_ROOT}/devkit/include \
		-o $@ $<

${VERSION}/%.mi: %.mi
	cp $< $@

clean:
	- rm ${LIBS}
