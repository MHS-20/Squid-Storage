#include "filetransfer.hpp"
using namespace std;

FileTransfer::FileTransfer(){}
FileTransfer::~FileTransfer() {}

void FileTransfer::sendFile(int socket, string rolename, string filepath)
{
    ifstream file(filepath, ios::binary | ios::ate);
    if (!file)
    {
        cerr << rolename + " Error opening file: " + filepath << endl; 
        return;
    }

    streamsize filesize = file.tellg();
    file.seekg(0, ios::beg);
    ssize_t bytes = send(socket, &filesize, sizeof(filesize), 0);
    if(!handleErrors(bytes))
    {
        return;
    }

    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
    {
        bytes = send(socket, buffer, file.gcount(), 0);
        if(!handleErrors(bytes))
        {
            return;
        }
    }
    cout << string(rolename) + " File sent \n";
    file.close();
}

void FileTransfer::receiveFile(int socket, string rolename, string outputpath)
{
    ofstream outfile(outputpath, ios::binary);
    if (!outfile)
    {
        cerr << rolename + " Error creating file: " <<endl;
        return;
    }

    streamsize filesize;
    ssize_t bytes = read(socket, &filesize, sizeof(filesize));
    if (!handleErrors(bytes))
    {
        // delete file 
        outfile.close();
        remove(outputpath.c_str());
        cerr << rolename + " Error receiving file size: " << endl;
        return;
    }

    char buffer[BUFFER_SIZE];
    while (filesize > 0)
    {
        int bytes_to_read = (filesize > BUFFER_SIZE) ? BUFFER_SIZE : filesize;
        int received = read(socket, buffer, bytes_to_read);
        if (!handleErrors(received))
        {
            // delete file
            outfile.close();
            remove(outputpath.c_str());
            cerr << rolename + " Error receiving file: " << endl;
            return;
        }

        outfile.write(buffer, received);
        filesize -= received;
    }

    cout << rolename + " File " + outputpath + " received \n";
    outfile.close();
}

bool FileTransfer::handleErrors(ssize_t bytes)
{
    if (bytes == 0)
    {
        cerr << "FileTransfer: Connection closed by peer" << endl;
        return false;
    }
    else if (bytes < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            cerr << "FileTransfer: Socket timeout" << endl;
        }
        return false;
    }

    return true;
}
