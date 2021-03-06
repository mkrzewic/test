CXX=g++ 
CXXFLAGS:=-g -Wall -I. -std=c++14 -ggdb -O2 -fno-omit-frame-pointer
ROOTLIBS = -L$(ROOTSYS)/lib -L$(ALIBUILD_WORK_DIR)/slc7_x86-64/boost/latest/lib -lCore -lHist -lGraf -lGraf3d -lGpad -lTree -lRint -lPostscript -lMatrix -lPhysics -lGui -lm -ldl -rdynamic -lThread -lMathCore -lGeom -lGraf -lMathCore -lNet -lTree -lEG -lGpad -lMatrix -lMinuit -lPhysics -lVMC -lThread -lXMLParser -lGraf3d -lRIO -lHist -lCore -lzmq -lbenchmark -lboost_container
ROOTINC = -I$(ROOTSYS)/include -I$(ALIBUILD_WORK_DIR)/slc7_x86-64/boost/latest/include/ -I/usr/local/include/benchmark -I/usr/local/include
CXXFLAGS+=$(ROOTINC)

INCLUDES:=fake.h test.h

OBJECTS:=test.o

SRCS:=test.cxx

all: test

test: $(SRCS) $(OBJECTS) $(INCLUDES)
	$(CXX) -o  $@  $(OBJECTS) $(CXXFLAGS) $(ROOTLIBS)

%.o: %.cxx $(INCLUDES)
	$(CXX) $(CXXFLAGS) -c $< 

clean: 
	rm -f *.o *~ test

very-clean:
	rm -f *.o *~ test

.PHONY: clean very-clean
#.SILENT:
