CXX = g++
CXXFLAGS = -std=c++11 -W -Wall -lstdc++
TARGET = testccp
OBJS = testccp.o serialize.o ctrlpath.o dpstate.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f *.o $(TARGET)
