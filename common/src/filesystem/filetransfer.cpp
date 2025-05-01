#include "filetransfer.hpp"
using namespace std;

FileTransfer::FileTransfer(){}
FileTransfer::~FileTransfer() {}

void FileTransfer::sendFile(int socket, const char *rolename, const char *filepath)
{
    ifstream file(filepath, ios::binary | ios::ate);
    if (!file)
    {
        cerr << string(rolename) + " Error opening file: "; 
        return;
    }

    streamsize filesize = file.tellg();
    file.seekg(0, ios::beg);
    send(socket, &filesize, sizeof(filesize), 0);

    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
    {
        send(socket, buffer, file.gcount(), 0);
    }
    cout << string(rolename) + " File sent \n";
    file.close();
}

void FileTransfer::receiveFile(int socket, const char *rolename, const char *outputpath)
{
    ofstream outfile(outputpath, ios::binary);
    if (!outfile)
    {
        cerr << string(rolename) + " Error creating file: " <<endl;
        return;
    }

    streamsize filesize;
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

    string msg = string(rolename) + " File " + string(outputpath) + " received \n";
    cout << msg.c_str();
    outfile.close();
}

bool handleErrors(ssize_t bytesRead)
{
    if (bytesRead == 0)
    {
        cerr << "FileTransfer: Connection closed by peer" << endl;
        // close(socket_fd);
        return false;
    }
    else if (bytesRead < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            cerr << "FileTransfer: Socket timeout" << endl;
            // close(socket_fd);
            // alive = false;
        }
        return false;
    }

    return true;
}
