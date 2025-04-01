#include "filetransfer.hpp"

FileTransfer::FileTransfer() {
}

FileTransfer::~FileTransfer() {
}

void FileTransfer::sendFile(int socket, const char *rolename, const char *filepath)
{
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        std::string msg = std::string(rolename) + " Error opening file: ";        
        perror(msg.c_str());
        return;
    }

    std::streamsize filesize = file.tellg();
    file.seekg(0, std::ios::beg);

    send(socket, &filesize, sizeof(filesize), 0);

    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
    {
        send(socket, buffer, file.gcount(), 0);
    }

    std::string msg = std::string(rolename) + " File sent \n";        
    std::cout << msg.c_str();
    file.close();
}

void FileTransfer::receiveFile(int socket, const char *rolename, const char *outputpath)
{
    std::ofstream outfile(outputpath, std::ios::binary);
    if (!outfile)
    {
        std::string msg = std::string(rolename) + " Error creating file: ";        
        perror(msg.c_str());
        return;
    }

    std::streamsize filesize;
    read(socket, &filesize, sizeof(filesize));

    char buffer[BUFFER_SIZE];
    while (filesize > 0)
    {
        int bytes_to_read = (filesize > BUFFER_SIZE) ? BUFFER_SIZE : filesize;
        int received = read(socket, buffer, bytes_to_read);
        if (received <= 0)
            break;

        outfile.write(buffer, received);
        filesize -= received;
    }

    std::string msg = std::string(rolename) + " File received \n";        
    std::cout << msg.c_str();
    outfile.close();
}
