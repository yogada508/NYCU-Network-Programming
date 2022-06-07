NPSHELL_SRCS = npshell.cpp 
EXT  = .cpp
NPSHELL_OBJS = $(NPSHELL_SRCS:$(EXT)=.o)
NPSHELL_EXEC = npshell
CXX  = g++
CXXFLAGS = -std=c++11

all:$(NPSHELL_EXEC) clean

$(NPSHELL_EXEC):$(NPSHELL_OBJS)
	$(CXX) $(CXXFLAGS) -o $(NPSHELL_EXEC) $(NPSHELL_OBJS)

%.o:%$(EXT)
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -rf *.o