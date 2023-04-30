CC=g++
CXX=g++

LIBNAME=libuthreads.a

LIBSRC=uthreads.cpp
LIBOBJ=$(LIBSRC:.cpp=.o)

CFLAGS = -Wall -std=c++11 -g $(INCS)
CXXFLAGS = -Wall -std=c++11 -g $(INCS)

TAR=tar
TARFLAGS=-cvf
TARNAME=ex2.tar
TARSRCS= $(LIBHT) $(LIBSRC) Makefile README

all: $(LIBNAME)

$(LIBNAME): $(LIBOBJT) $(LIBOBJ)
	ar rcs $(LIBNAME) $(LIBOBJT) $(LIBOBJ)
clean:
	$(RM) $(TARGETS) $(OBJ) $(LIBOBJT) $(LIBOBJ) *~ *core

tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
