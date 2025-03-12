CXX = g++
CXXFLAGS = -std=c++11 -I./src
SRC = src/datanode.cpp
TARGET = datanode

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)