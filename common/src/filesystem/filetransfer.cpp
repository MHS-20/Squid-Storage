#include "filetransfer.hpp"
using namespace std;

static ssize_t sendAll(INetworkChannel &channel, const void *buf, size_t len)
{
    size_t total = 0;
    const uint8_t *p = static_cast<const uint8_t *>(buf);
    while (total < len)
    {
        ssize_t n = channel.writeBytes(p + total, len - total);
        if (n > 0) { total += static_cast<size_t>(n); continue; }
        if (n == 0) return 0;
        return -1;
    }
    return static_cast<ssize_t>(total);
}

static ssize_t recvAll(INetworkChannel &channel, void *buf, size_t len)
{
    size_t total = 0;
    uint8_t *p = static_cast<uint8_t *>(buf);
    while (total < len)
    {
        ssize_t n = channel.readBytes(p + total, len - total);
        if (n > 0) { total += static_cast<size_t>(n); continue; }
        if (n == 0) return 0;
        return -1;
    }
    return static_cast<ssize_t>(total);
}

FileTransfer::FileTransfer(){}
FileTransfer::~FileTransfer() {}

bool FileTransfer::sendFile(INetworkChannel &channel, const string &rolename, const string &filepath)
{
    ifstream file(filepath, ios::binary | ios::ate);
    if (!file)
    {
        cerr << rolename + " Error opening file: " + filepath << endl;
        return false;
    }

    streamsize ssize = file.tellg();
    if (ssize < 0)
    {
        cerr << rolename + " Error determining file size: " + filepath << endl;
        file.close();
        return false;
    }

    uint64_t filesize = static_cast<uint64_t>(ssize);
    if (filesize > FILETRANSFER_MAX_SIZE)
    {
        cerr << rolename + " File too large to send: " << filesize << endl;
        file.close();
        return false;
    }

    file.seekg(0, ios::beg);

    // send 8-byte big-endian filesize
    uint8_t sizebuf[8];
    for (int i = 7; i >= 0; --i)
    {
        sizebuf[i] = static_cast<uint8_t>(filesize & 0xFF);
        filesize >>= 8;
    }

    // send size
    ssize_t bytes = sendAll(channel, sizebuf, sizeof(sizebuf));
    if(!handleErrors(bytes))
    {
        file.close();
        return false;
    }

    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
    {
        streamsize toSend = file.gcount();
        ssize_t s = sendAll(channel, buffer, static_cast<size_t>(toSend));
        if(!handleErrors(s))
        {
            file.close();
            return false;
        }
    }
    cout << string(rolename) + " File sent \n";
    file.close();
    return true;
}

bool FileTransfer::receiveFile(INetworkChannel &channel, const string &rolename, const string &outputpath)
{
    ofstream outfile(outputpath, ios::binary);
    if (!outfile)
    {
        cerr << rolename + " Error creating file: " << outputpath << endl;
        return false;
    }

    uint8_t sizebuf[8];
    ssize_t bytes = recvAll(channel, sizebuf, sizeof(sizebuf));
    if (!handleErrors(bytes))
    {
        // delete file
        outfile.close();
        remove(outputpath.c_str());
        cerr << rolename + " Error receiving file size: " << endl;
        return false;
    }

    uint64_t filesize = 0;
    for (int i = 0; i < 8; ++i)
        filesize = (filesize << 8) | static_cast<uint64_t>(sizebuf[i]);

    if (filesize > FILETRANSFER_MAX_SIZE)
    {
        outfile.close();
        remove(outputpath.c_str());
        cerr << rolename + " File too large to receive: " << filesize << endl;
        return false;
    }

    char buffer[BUFFER_SIZE];
    uint64_t remaining = filesize;
    while (remaining > 0)
    {
        size_t chunk = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : static_cast<size_t>(remaining);
        ssize_t r = recvAll(channel, buffer, chunk);
        if (!handleErrors(r))
        {
            // delete file
            outfile.close();
            remove(outputpath.c_str());
            cerr << rolename + " Error receiving file: " << endl;
            return false;
        }

        outfile.write(buffer, static_cast<std::streamsize>(r));
        remaining -= static_cast<uint64_t>(r);
    }

    cout << rolename + " File " + outputpath + " received \n";
    outfile.close();
    return true;
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
