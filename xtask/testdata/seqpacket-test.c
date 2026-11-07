// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
int main(void) {
    int s[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0,s)==0);
    int type=0;socklen_t len=sizeof type;assert(getsockopt(s[0],SOL_SOCKET,SO_TYPE,&type,&len)==0&&type==SOCK_SEQPACKET);
    int capacity=0;len=sizeof capacity;assert(getsockopt(s[0],SOL_SOCKET,SO_SNDBUF,&capacity,&len)==0&&capacity==131072&&len==sizeof capacity);
    int receive_capacity=0;len=sizeof receive_capacity;assert(getsockopt(s[1],SOL_SOCKET,SO_RCVBUF,&receive_capacity,&len)==0&&receive_capacity==capacity);
    unsigned char short_value[5]={0xa5,0xa5,0xa5,0xa5,0xa5};len=2;
    assert(getsockopt(s[0],SOL_SOCKET,SO_SNDBUF,short_value,&len)==0&&len==2&&short_value[2]==0xa5&&short_value[4]==0xa5);
    len=0;assert(getsockopt(s[0],SOL_SOCKET,SO_SNDBUF,short_value,&len)==0&&len==0&&short_value[2]==0xa5);
    int requested=262144;errno=0;assert(setsockopt(s[0],SOL_SOCKET,SO_SNDBUF,&requested,sizeof requested)==-1&&errno==EOPNOTSUPP);
    assert(fcntl(s[0],F_GETFD)&FD_CLOEXEC);assert(fcntl(s[0],F_GETFL)&O_NONBLOCK);
    assert(send(s[0],"ABCDE",5,0)==5&&send(s[0],"xy",2,0)==2);
    int available=-1;assert(ioctl(s[1],FIONREAD,&available)==0&&available==5);
    char b[32]={0};struct iovec io={b,2};struct msghdr m={.msg_iov=&io,.msg_iovlen=1};
    assert(recvmsg(s[1],&m,MSG_PEEK)==2&&m.msg_flags&MSG_TRUNC&&!memcmp(b,"AB",2));
    assert(recvmsg(s[1],&m,0)==2&&m.msg_flags&MSG_TRUNC);
    assert(ioctl(s[1],FIONREAD,&available)==0&&available==2);
    assert(recv(s[1],b,sizeof b,0)==2&&!memcmp(b,"xy",2));
    errno=0;assert(recv(s[1],b,sizeof b,0)==-1&&errno==EAGAIN);
    assert(send(s[0],"",0,0)==0);struct pollfd p={s[1],POLLIN,0};assert(poll(&p,1,0)==1&&p.revents&POLLIN);
    assert(recv(s[1],b,sizeof b,0)==0);errno=0;assert(recv(s[1],b,sizeof b,0)==-1&&errno==EAGAIN);
    char *large=malloc(131073);assert(large);memset(large,'q',131073);
    errno=0;assert(send(s[0],large,131073,0)==-1&&errno==EMSGSIZE);
    struct iovec big_iov[2]={{large,70000},{large+70000,61072}};struct msghdr big_msg={.msg_iov=big_iov,.msg_iovlen=2};
    assert(sendmsg(s[0],&big_msg,0)==131072);errno=0;assert(send(s[0],"z",1,0)==-1&&errno==EAGAIN);
    io.iov_len=sizeof b;assert(recvmsg(s[1],&m,MSG_TRUNC)==131072&&m.msg_flags&MSG_TRUNC);assert(b[0]=='q');
    assert(sendmsg(s[0],&big_msg,0)==131072);
    struct iovec full_iov={large,131072};struct msghdr full_msg={.msg_iov=&full_iov,.msg_iovlen=1};
    assert(recvmsg(s[1],&full_msg,0)==131072&&!(full_msg.msg_flags&MSG_TRUNC));for(int i=0;i<131072;i++)assert(large[i]=='q');free(large);
    int on=1;assert(setsockopt(s[1],SOL_SOCKET,SO_PASSCRED,&on,sizeof on)==0);
    struct ucred peer={0};len=sizeof peer;assert(getsockopt(s[1],SOL_SOCKET,SO_PEERCRED,&peer,&len)==0&&peer.pid==getpid()&&peer.uid==getuid());
    int f=open("/tmp/seqpacket-file",O_CREAT|O_TRUNC|O_RDWR,0600);assert(f>=0&&write(f,"descriptor-data",15)==15&&lseek(f,0,SEEK_SET)==0);
    pid_t child=fork();assert(child>=0);
    if(!child) {
        close(s[1]);char control[CMSG_SPACE(sizeof(int))]={0};struct iovec i={"R",1};
        struct msghdr msg={.msg_iov=&i,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof control};
        struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(c),&f,sizeof f);
        assert(sendmsg(s[0],&msg,0)==1);_exit(0);
    }
    close(s[0]);int st;assert(waitpid(child,&st,0)==child&&WIFEXITED(st)&&WEXITSTATUS(st)==0);
    char control[CMSG_SPACE(sizeof(struct ucred))+CMSG_SPACE(sizeof(int))]={0};
    m=(struct msghdr){.msg_iov=&io,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof control};
    assert(recvmsg(s[1],&m,MSG_CMSG_CLOEXEC)==1&&b[0]=='R'&&!(m.msg_flags&MSG_CTRUNC));
    int received=-1;struct ucred cred={0};
    for(struct cmsghdr *c=CMSG_FIRSTHDR(&m);c;c=CMSG_NXTHDR(&m,c)) {
        if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS)memcpy(&received,CMSG_DATA(c),sizeof received);
        if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_CREDENTIALS)memcpy(&cred,CMSG_DATA(c),sizeof cred);
    }
    assert(cred.pid==child&&cred.uid==getuid()&&cred.gid==getgid());assert(received>=0&&fcntl(received,F_GETFD)&FD_CLOEXEC);
    assert(read(received,b,15)==15&&!memcmp(b,"descriptor-data",15));assert(recv(s[1],b,sizeof b,0)==0);
    close(received);close(f);close(s[1]);unlink("/tmp/seqpacket-file");
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);assert(write(s[0],"abc",3)==3&&write(s[0],"defg",4)==4);
    assert(ioctl(s[1],FIONREAD,&available)==0&&available==7);assert(read(s[1],b,2)==2);
    assert(ioctl(s[1],FIONREAD,&available)==0&&available==5);close(s[0]);close(s[1]);
    assert(socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK,0,s)==0);
    len=sizeof capacity;assert(getsockopt(s[0],SOL_SOCKET,SO_SNDBUF,&capacity,&len)==0&&capacity>0);
    large=malloc(capacity);assert(large);memset(large,'s',capacity);
    assert(send(s[0],large,capacity,0)==capacity);errno=0;assert(send(s[0],"x",1,0)==-1&&errno==EAGAIN);
    assert(recv(s[1],large,capacity,0)==capacity);for(int i=0;i<capacity;i++)assert(large[i]=='s');
    assert(send(s[0],"x",1,0)==1&&recv(s[1],b,sizeof b,0)==1&&b[0]=='x');
    for(int i=0;i<capacity;i++)large[i]=(char)(i%251);
    size_t offset=0;char *copy=malloc(capacity);assert(copy);
    while(offset<(size_t)capacity) {
        struct iovec chunks[2]={{large+offset,(size_t)capacity-offset-1},{large+capacity-1,1}};
        struct msghdr message={.msg_iov=chunks,.msg_iovlen=2};
        ssize_t sent=sendmsg(s[0],&message,MSG_DONTWAIT);assert(sent>0&&sent<=65536);
        struct iovec output={copy,(size_t)sent};struct msghdr received_msg={.msg_iov=&output,.msg_iovlen=1};
        ssize_t got=recvmsg(s[1],&received_msg,0);assert(got==sent&&!memcmp(copy,large+offset,sent));offset+=sent;
    }
    free(copy);free(large);close(s[0]);close(s[1]);
    puts("seqpacket-test-done: boundaries, peek, truncation, zero packet, backpressure, fork credentials and fd transfer");return 0;
}
