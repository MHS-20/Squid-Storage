#include <iostream>
#include <thread>
#include "server.hpp"
#include "squidprotocol.hpp"

using namespace std;
int main()
{
    Server server = Server(12345);
    SquidProtocol squidProtocol = SquidProtocol(server.getSocket(), "server", "SERVER");    

    server.start();
    int socket = server.getSocket();

    while (true)
    {
        squidProtocol.requestDispatcher(squidProtocol.receiveAndParseMessage());
    }

}