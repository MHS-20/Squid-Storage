CXX = g++
CXXFLAGS = -std=c++11 -I./src
SRC = src/server.cpp
TARGET = server

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)