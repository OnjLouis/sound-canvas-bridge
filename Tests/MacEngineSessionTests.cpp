#include "MacEngineSession.h"
#include <fcntl.h>
#include <cstdlib>
#include <cassert>

int main() {
    int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    char path[]="/tmp/scbridge-session-test.XXXXXX";
    const int memory=mkstemp(path);assert(memory>=0);unlink(path);
    assert(ftruncate(memory,4096)==0);
    assert(scbridge::macSession::sendMemory(sockets[0],memory));
    const int duplicate=scbridge::macSession::receiveMemory(sockets[1],4096);
    assert(duplicate>=0);close(duplicate);
    assert(scbridge::macSession::sendMemory(sockets[0],memory));
    assert(scbridge::macSession::receiveMemory(sockets[1],8192)<0);
    assert(fchmod(memory,0644)==0);
    assert(scbridge::macSession::sendMemory(sockets[0],memory));
    assert(scbridge::macSession::receiveMemory(sockets[1],4096)<0);
    char invalid='F';assert(write(sockets[0],&invalid,1)==1);
    assert(scbridge::macSession::receiveMemory(sockets[1],4096)<0);
    close(memory);close(sockets[0]);close(sockets[1]);
}
