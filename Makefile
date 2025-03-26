CXX = g++
CXXFLAGS = -std=c++17 -Wall -g
LDFLAGS = -pthread -lstdc++fs

INCLUDES = -Iserver/src -Idata-node/src -Iclient/src -Icommon/src

MAIN_SRC = main.cpp
SERVER_SRC = server/src/server.cpp
DATANODE_SRC = data-node/src/datanode.cpp
CLIENT_SRC = client/src/client.cpp

SQUIDPROTOCOL_SRC = common/src/squidprotocol.cpp
SQUIDFORMATTER_SRC = common/src/squidprotocolformatter.cpp

FILELOCK_SRC = client/src/filelock.cpp
FILETRANSFER_SRC = common/src/filetransfer.cpp
FILEMANAGER_SRC = client/src/filemanager.cpp

MAIN_OBJ = $(MAIN_SRC:.cpp=.o)
SERVER_OBJ = $(SERVER_SRC:.cpp=.o)
DATANODE_OBJ = $(DATANODE_SRC:.cpp=.o)
CLIENT_OBJ = $(CLIENT_SRC:.cpp=.o)

SQUIDPROTOCOL_OBJ = $(SQUIDPROTOCOL_SRC:.cpp=.o)
SQUIDFORMATTER_OBJ = $(SQUIDFORMATTER_SRC:.cpp=.o)

FILELOCK_OBJ = $(FILELOCK_SRC:.cpp=.o)
FILETRANSFER_OBJ = $(FILETRANSFER_SRC:.cpp=.o)
FILEMANAGER_OBJ = $(FILEMANAGER_SRC:.cpp=.o)

all: main

main: $(MAIN_OBJ) $(SERVER_OBJ) $(DATANODE_OBJ) $(CLIENT_OBJ) $(FILELOCK_OBJ) $(FILETRANSFER_OBJ) $(SQUIDPROTOCOL_OBJ) $(SQUIDFORMATTER_OBJ) $(FILEMANAGER_OBJ)
		$(CXX) $(CXXFLAGS) -o main $(MAIN_OBJ) $(SERVER_OBJ) $(DATANODE_OBJ) $(CLIENT_OBJ) $(FILELOCK_OBJ) $(FILETRANSFER_OBJ) $(SQUIDPROTOCOL_OBJ) $(SQUIDFORMATTER_OBJ) $(FILEMANAGER_OBJ) $(LDFLAGS) 

%.o: %.cpp
		$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
		rm -f main $(MAIN_OBJ) $(SERVER_OBJ) $(DATANODE_OBJ) $(CLIENT_OBJ) $(FILELOCK_OBJ) $(FILETRANSFER_OBJ) $(SQUIDPROTOCOL_OBJ) $(SQUIDFORMATTER_OBJ) $(FILEMANAGER_OBJ)

run: main
		./main