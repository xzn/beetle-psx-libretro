ifeq ($(platform),)
	platform = unix
	ifeq ($(shell uname -s),)
		platform = win
	else ifneq ($(findstring MINGW,$(shell uname -s)),)
		platform = win
	endif
endif

ifeq ($(platform), win)
	GLSLANG_PLATFORM = Windows
else
	GLSLANG_PLATFORM = Unix
endif

GLSLANG_INCFLAGS += -Iglslang \
	-Iglslang/glslang/OSDependent/$(GLSLANG_PLATFORM) \
	-Iglslang/OGLCompilersDLL \
	-Iglslang/glslang/MachineIndependent \
	-Iglslang/glslang/Public \
	-Iglslang/SPIRV

GLSLANG_SOURCES_CXX = \
	$(wildcard glslang/SPIRV/*.cpp) \
	$(wildcard glslang/OGLCompilersDLL/*.cpp) \
	$(wildcard glslang/glslang/GenericCodeGen/*.cpp) \
	$(wildcard glslang/glslang/MachineIndependent/*.cpp) \
	$(wildcard glslang/glslang/MachineIndependent/preprocessor/*.cpp) \
	glslang/glslang/OSDependent/$(GLSLANG_PLATFORM)/ossource.cpp

SOURCES_CXX = $(wildcard *.cpp)

INCFLAGS += $(GLSLANG_INCFLAGS)
SOURCES_CXX += $(GLSLANG_SOURCES_CXX)

ifeq ($(platform), win)
	LDFLAGS += -static
endif
FLAGS += $(INCFLAGS)
CXXFLAGS += -DNDEBUG -MMD
PTHREAD_FLAGS = -lpthread
ifeq ($(platform), win)
else
	LIBS += $(PTHREAD_FLAGS)
endif

OBJOUT = -o
LINKOUT = -o

TARGET_NAME = rebuild_shaders
ifeq ($(platform), win)
	TARGET = $(TARGET_NAME).exe
else
	TARGET = $(TARGET_NAME)
endif

OBJECTS = $(SOURCES_CXX:.cpp=.o)
DEPS = $(SOURCES_CXX:.cpp=.d)
CXXFLAGS += $(FLAGS)

all: $(TARGET)

-include $(DEPS)

$(TARGET): $(OBJECTS)
	$(CXX) $(LINKOUT)$@ $^ $(LDFLAGS) $(LIBS)

%.o: %.cpp
	$(CXX) -c $(OBJOUT)$@ $< $(CXXFLAGS)

clean:
	rm -f $(OBJECTS)
	rm -f $(DEPS)
	rm -f $(TARGET)

.PHONY: clean
