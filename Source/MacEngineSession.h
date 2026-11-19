#pragma once
#include <sys/socket.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>

namespace scbridge::macSession {
inline constexpr uint32_t magic=0x53434c31;
inline constexpr int startupTimeoutMs=15000;
struct Hello {
    uint32_t marker=magic;
    std::array<uint8_t,16> nonce{};
};
inline bool wait(int fd,short events,int milliseconds=startupTimeoutMs) {
    pollfd descriptor{fd,events,0};
    int result;
    do {result=poll(&descriptor,1,milliseconds);} while(result<0 && errno==EINTR);
    return result>0 && (descriptor.revents&events)!=0;
}
inline bool transfer(int fd,void* data,size_t bytes,bool sending) {
    auto* position=static_cast<char*>(data);
    while(bytes) {
        if(!wait(fd,sending ? POLLOUT : POLLIN)) return false;
        const auto count=sending ? send(fd,position,bytes,MSG_DONTWAIT) : recv(fd,position,bytes,MSG_DONTWAIT);
        if(count<0 && (errno==EINTR || errno==EAGAIN)) continue;
        if(count<=0) return false;
        bytes-=size_t(count);position+=count;
    }
    return true;
}
inline bool sendMemory(int socket,int memory) {
    char marker='F';iovec vector{&marker,1};
    alignas(cmsghdr) char ancillary[CMSG_SPACE(sizeof(int))]{};
    msghdr message{};message.msg_iov=&vector;message.msg_iovlen=1;
    message.msg_control=ancillary;message.msg_controllen=sizeof(ancillary);
    auto* control=CMSG_FIRSTHDR(&message);
    control->cmsg_level=SOL_SOCKET;control->cmsg_type=SCM_RIGHTS;control->cmsg_len=CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(control),&memory,sizeof(memory));
    return wait(socket,POLLOUT) && sendmsg(socket,&message,MSG_DONTWAIT)==1;
}
inline int receiveMemory(int socket,size_t expectedSize) {
    if(!wait(socket,POLLIN)) return -1;
    char marker=0;iovec vector{&marker,1};
    alignas(cmsghdr) char ancillary[CMSG_SPACE(sizeof(int)*8)]{};
    msghdr message{};message.msg_iov=&vector;message.msg_iovlen=1;
    message.msg_control=ancillary;message.msg_controllen=sizeof(ancillary);
    if(recvmsg(socket,&message,MSG_DONTWAIT)!=1) return -1;
    int memory=-1;unsigned count=0;
    for(auto* control=CMSG_FIRSTHDR(&message);control;control=CMSG_NXTHDR(&message,control)) {
        if(control->cmsg_level!=SOL_SOCKET || control->cmsg_type!=SCM_RIGHTS || control->cmsg_len<CMSG_LEN(0)) continue;
        const auto descriptors=(control->cmsg_len-CMSG_LEN(0))/sizeof(int);
        for(size_t index=0;index<descriptors;++index) {
            int received;memcpy(&received,CMSG_DATA(control)+index*sizeof(int),sizeof(received));
            if(count++==0) memory=received;else close(received);
        }
    }
    struct stat status{};
    if(marker!='F' || count!=1 || (message.msg_flags&(MSG_CTRUNC|MSG_TRUNC)) ||
       memory<0 || fstat(memory,&status) || !S_ISREG(status.st_mode) || status.st_uid!=geteuid() ||
       (status.st_mode&0777)!=0600 || status.st_size!=static_cast<off_t>(expectedSize)) {
        if(memory>=0) close(memory);
        return -1;
    }
    return memory;
}
}
