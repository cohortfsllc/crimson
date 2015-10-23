SEASTAR_PC ?= seastar.pc

SEASTAR_CXXFLAGS := $(shell pkg-config --cflags $(SEASTAR_PC))
SEASTAR_LIBS := $(shell pkg-config --libs $(SEASTAR_PC))

CXXFLAGS := -std=c++14 $(SEASTAR_CXXFLAGS) $(CXXFLAGS)


SRC := src/crimson.cc
OBJ := $(patsubst %.cc, %.o, $(filter %.cc, $(SRC))) \
       $(patsubst %.c, %.o, $(filter %.c, $(SRC)))
DEP := $(patsubst %.cc, %.d, $(filter %.cc, $(SRC))) \
       $(patsubst %.c, %.d, $(filter %.c, $(SRC)))

crimson: $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LDFLAGS) $(SEASTAR_LIBS)

.PHONY: clean
clean:
	-rm -f $(OBJ) $(DEP) crimson

%.d: %.c
	./depend.sh $(shell dirname $*) $(CC) $(CFLAGS) $*.c > $@

%.d: %.cc
	./depend.sh $(shell dirname $*) $(CXX) $(CXXFLAGS) $*.cc > $@

-include $(OBJ:.o=.d)
