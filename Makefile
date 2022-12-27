CC	= gcc
CXX = g++
INC_PATH = .

CFLAGS	= -O2 -g -Wall -Wno-deprecated -std=c++11 -fPIC
CFLAGS 	+= -I$(INC_PATH) $(INCLUDE)
CFLAGS  += ${FLAGS}
LDFLAGS	= -L/usr/lib -lpthread -ldl

# 输出文件名
COSERVER = libcoserver
COSERVER_STATIC = ./lib/$(COSERVER).a
COSERVER_SHARED = ./lib/$(COSERVER).so
OUTPUT_PATH = ./obj


#设置VPATH 包含源码的子目录列表
#添加源文件
SUBINC = coserver
#添加头文件
SUBDIR = coserver coserver/base coserver/core coserver/protocol coserver/upstream

#设置VPATH
INCLUDE = $(foreach n, $(SUBINC), -I$(INC_PATH)/$(n))
SPACE 	= 
VPATH 	= $(subst $(SPACE),, $(strip $(foreach n,$(SUBDIR), $(INC_PATH)/$(n)))) $(OUTPUT_PATH)

C_SOURCES 	= $(notdir $(foreach n, $(SUBDIR), $(wildcard $(INC_PATH)/$(n)/*.c)))
CPP_SOURCES = $(notdir $(foreach n, $(SUBDIR), $(wildcard $(INC_PATH)/$(n)/*.cpp)))

C_OBJECTS 	= $(patsubst  %.c,  $(OUTPUT_PATH)/%.o, $(C_SOURCES))
CPP_OBJECTS = $(patsubst  %.cpp,  $(OUTPUT_PATH)/%.o, $(CPP_SOURCES))

CXX_SOURCES = $(CPP_SOURCES) $(C_SOURCES)
CXX_OBJECTS = $(CPP_OBJECTS) $(C_OBJECTS)


#all: $(COSERVER_STATIC) $(COSERVER_SHARED)
all: mkdir $(COSERVER_SHARED)

$(COSERVER_STATIC):$(CXX_OBJECTS)
	ar cr -o $@ $(foreach n, $(CXX_OBJECTS), $(n)) $(foreach n, $(OBJS), $(n))
	#******************************************************************************#
	#                          build static successful !                           #
	#******************************************************************************#

$(COSERVER_SHARED):$(CXX_OBJECTS)
	$(CXX) -shared -o $@ $(foreach n, $(CXX_OBJECTS), $(n)) $(foreach n, $(OBJS), $(n))  $(LDFLAGS)
	#******************************************************************************#
	#                          build shared successful !                           #
	#******************************************************************************#

$(OUTPUT_PATH)/%.o:%.cpp
	$(CXX) $< -c $(CFLAGS) -o $@
	
$(OUTPUT_PATH)/%.o:%.c
	$(CC) $< -c $(CFLAGS) -o $@

mkdir:
	mkdir -p $(dir $(COSERVER_STATIC))
	mkdir -p $(OUTPUT_PATH)
	
rmdir:
	rm -rf $(dir $(COSERVER_STATIC))
	rm -rf $(OUTPUT_PATH)

clean:
	rm -f $(OUTPUT_PATH)/*
	rm -rf $(COSERVER_STATIC) ${COSERVER_SHARED}

install:
	find coserver -name *.h | xargs -I{} cp --parents {} /usr/local/include/
	cp -rf ${COSERVER_SHARED} /usr/local/lib64

