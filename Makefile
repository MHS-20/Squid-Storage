CXX = g++
CXXFLAGS = -std=c++17 -Wall -g #-fsanitize=address
LDFLAGS = -pthread -lstdc++fs

INCLUDES = -Iserver/src -Idata-node/src -Iclient/src -Icommon/src/squidprotocol -Icommon/src/peer -Icommon/src/filesystem 

MAIN_SRC = main.cpp
SERVER_SRC = server/src/server.cpp
DATANODE_SRC = data-node/src/datanode.cpp
CLIENT_SRC = client/src/client.cpp
PEER_SRC = common/src/peer/peer.cpp

SQUIDPROTOCOL_SRC = common/src/squidprotocol/squidprotocol.cpp
SQUIDFORMATTER_SRC = common/src/squidprotocol/squidProtocolFormatter.cpp
SQUIDSERVER_SRC = server/src/squidProtocolServer.cpp
# SQUIDACTIVE_SRC = common/src/squidprotocol/squidProtocolActive.cpp
# SQUIDPASSIVE_SRC = common/src/squidprotocol/squidProtocolPassive.cpp
# SQUIDCOMMUNICATOR_SRC = common/src/squidprotocol/squidProtocolCommunicator.cpp

FILELOCK_SRC = common/src/filesystem/filelock.cpp
FILETRANSFER_SRC = common/src/filesystem/filetransfer.cpp
FILEMANAGER_SRC = common/src/filesystem/filemanager.cpp

MAIN_OBJ = $(MAIN_SRC:.cpp=.o)
SERVER_OBJ = $(SERVER_SRC:.cpp=.o)
DATANODE_OBJ = $(DATANODE_SRC:.cpp=.o)
CLIENT_OBJ = $(CLIENT_SRC:.cpp=.o)
PEER_OBJ = $(PEER_SRC:.cpp=.o)

SQUIDPROTOCOL_OBJ = $(SQUIDPROTOCOL_SRC:.cpp=.o)
SQUIDFORMATTER_OBJ = $(SQUIDFORMATTER_SRC:.cpp=.o)
SQUIDSERVER_OBJ = $(SQUIDSERVER_SRC:.cpp=.o)
# SQUIDACTIVE_OBJ = $(SQUIDACTIVE_SRC:.cpp=.o)
# SQUIDPASSIVE_OBJ = $(SQUIDPASSIVE_SRC:.cpp=.o)
# SQUIDCOMMUNICATOR_OBJ = $(SQUIDCOMMUNICATOR_SRC:.cpp=.o)

FILELOCK_OBJ = $(FILELOCK_SRC:.cpp=.o)
FILETRANSFER_OBJ = $(FILETRANSFER_SRC:.cpp=.o)
FILEMANAGER_OBJ = $(FILEMANAGER_SRC:.cpp=.o)

all: main

main: $(MAIN_OBJ) $(SERVER_OBJ) $(DATANODE_OBJ) $(CLIENT_OBJ) $(FILELOCK_OBJ) $(FILETRANSFER_OBJ) $(SQUIDPROTOCOL_OBJ) $(SQUIDFORMATTER_OBJ) $(FILEMANAGER_OBJ) $(PEER_OBJ) $(SQUIDSERVER_OBJ) 
		$(CXX) $(CXXFLAGS) -o main $(MAIN_OBJ) $(SERVER_OBJ) $(DATANODE_OBJ) $(CLIENT_OBJ) $(FILELOCK_OBJ) $(FILETRANSFER_OBJ) $(SQUIDPROTOCOL_OBJ) $(SQUIDFORMATTER_OBJ) $(FILEMANAGER_OBJ) $(PEER_OBJ) $(SQUIDSERVER_OBJ) $(LDFLAGS) 

%.o: %.cpp
		$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
		rm -f main $(MAIN_OBJ) $(SERVER_OBJ) $(DATANODE_OBJ) $(CLIENT_OBJ) $(FILELOCK_OBJ) $(FILETRANSFER_OBJ) $(SQUIDPROTOCOL_OBJ) $(SQUIDFORMATTER_OBJ) $(FILEMANAGER_OBJ) $(PEER_OBJ) $(SQUIDSERVER_OBJ)

run: main
		./main