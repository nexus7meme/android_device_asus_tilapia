// SPDX-License-Identifier: Apache-2.0
// Bounded loopback fixtures only; never destroys another process's sockets.
#include <arpa/inet.h>
#include <errno.h>
#include <grp.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d %s errno=%d\n",__FILE__,__LINE__,#x,errno); return 1; } } while(0)
#define DIAG 20
#define DESTROY 21
static int nlopen(void) {
 int fd=socket(AF_NETLINK,SOCK_RAW|SOCK_CLOEXEC,NETLINK_INET_DIAG);
 struct timeval tv={3,0}; if(fd>=0)setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));return fd;
}
static int sendreq(int fd,int type,int family,int dump,const struct inet_diag_sockid *id) {
 struct {struct nlmsghdr h;struct inet_diag_req_v2 r;} q={0};
 q.h.nlmsg_len=sizeof(q);q.h.nlmsg_type=type;q.h.nlmsg_seq=1;
 q.h.nlmsg_flags=NLM_F_REQUEST|(dump?NLM_F_DUMP:NLM_F_ACK);
 q.r.sdiag_family=family;q.r.sdiag_protocol=IPPROTO_TCP;q.r.idiag_states=~0U;
 q.r.idiag_ext=1<<(INET_DIAG_INFO-1);
 if(id)q.r.id=*id;
 struct sockaddr_nl a={.nl_family=AF_NETLINK};
 return sendto(fd,&q,sizeof(q),0,(void*)&a,sizeof(a));
}
static int ackerror(int fd) {
 char b[8192];int n=recv(fd,b,sizeof(b),0);if(n<0)return -errno;
 struct nlmsghdr *h=(void*)b;
 if(n<(int)NLMSG_LENGTH(sizeof(struct nlmsgerr))||h->nlmsg_type!=NLMSG_ERROR)return -999;
 return ((struct nlmsgerr*)NLMSG_DATA(h))->error;
}
static int fixture(int family) {
 int listener=socket(family,SOCK_STREAM|SOCK_CLOEXEC,0);CHECK(listener>=0);
 struct sockaddr_storage a={0};socklen_t alen;
 if(family==AF_INET){struct sockaddr_in *v=(void*)&a;v->sin_family=family;v->sin_addr.s_addr=htonl(INADDR_LOOPBACK);alen=sizeof(*v);}
 else {struct sockaddr_in6 *v=(void*)&a;v->sin6_family=family;v->sin6_addr=in6addr_loopback;alen=sizeof(*v);}
 CHECK(bind(listener,(void*)&a,alen)==0);CHECK(listen(listener,1)==0);
 CHECK(getsockname(listener,(void*)&a,&alen)==0);
 int client=socket(family,SOCK_STREAM|SOCK_CLOEXEC,0);CHECK(client>=0);
 unsigned mark=0x1234;
 CHECK(connect(client,(void*)&a,alen)==0);int peer=accept(listener,NULL,NULL);CHECK(peer>=0);
 // Android libnetd_client assigns its network mark during connect. Set our
 // fixture marker afterwards so the dump comparison tests the kernel value.
 CHECK(setsockopt(client,SOL_SOCKET,SO_MARK,&mark,sizeof(mark))==0);
 char data[4096],got[4096];memset(data,0x5a,sizeof(data));
 CHECK(send(client,data,sizeof(data),MSG_NOSIGNAL)==sizeof(data));
 CHECK(recv(peer,got,sizeof(got),MSG_WAITALL)==sizeof(got));CHECK(!memcmp(data,got,sizeof(data)));
 CHECK(send(peer,data,sizeof(data),MSG_NOSIGNAL)==sizeof(data));
 CHECK(recv(client,got,sizeof(got),MSG_WAITALL)==sizeof(got));CHECK(!memcmp(data,got,sizeof(data)));
 usleep(100000);
 char info[256]={0};socklen_t ilen=sizeof(info);CHECK(getsockopt(client,IPPROTO_TCP,TCP_INFO,info,&ilen)==0);
 CHECK(ilen>=144);unsigned out,in;unsigned long long bytes;
 memcpy(&out,info+136,4);memcpy(&in,info+140,4);memcpy(&bytes,info+128,8);
 CHECK(out>0&&in>0&&bytes>=sizeof(data));
 CHECK(getsockname(client,(void*)&a,&alen)==0);
 unsigned short port=family==AF_INET?((struct sockaddr_in*)&a)->sin_port:((struct sockaddr_in6*)&a)->sin6_port;
 int nl=nlopen();CHECK(nl>=0);CHECK(sendreq(nl,DIAG,family,1,NULL)>0);
 int found=0,done=0;struct inet_diag_sockid id={0};
 while(!done){char b[32768];int n=recv(nl,b,sizeof(b),0);CHECK(n>0);
  for(struct nlmsghdr *h=(void*)b;NLMSG_OK(h,n);h=NLMSG_NEXT(h,n)){
   if(h->nlmsg_type==NLMSG_DONE){done=1;break;}
   CHECK(h->nlmsg_type!=NLMSG_ERROR);CHECK(h->nlmsg_type==DIAG);
   CHECK(h->nlmsg_len>=NLMSG_LENGTH(sizeof(struct inet_diag_msg)));
   struct inet_diag_msg *m=NLMSG_DATA(h);CHECK(m->idiag_family==family);
   if(m->id.idiag_sport!=port)continue;
   int left=h->nlmsg_len-NLMSG_LENGTH(sizeof(*m)),hasmark=0,hasinfo=0;
   for(struct rtattr *r=(void*)((char*)m+NLMSG_ALIGN(sizeof(*m)));RTA_OK(r,left);r=RTA_NEXT(r,left)){
    if(r->rta_type==15){unsigned v;CHECK(RTA_PAYLOAD(r)>=4);memcpy(&v,RTA_DATA(r),4);CHECK(v==mark);hasmark=1;}
    if(r->rta_type==INET_DIAG_INFO){CHECK(RTA_PAYLOAD(r)>=144);hasinfo=1;}
   }
   CHECK(hasmark&&hasinfo);id=m->id;found++;
  }
 }
 CHECK(found==1);close(nl);
 struct inet_diag_sockid stale=id;stale.idiag_cookie[0]^=0x123456;
 nl=nlopen();CHECK(nl>=0);CHECK(sendreq(nl,DESTROY,family,0,&stale)>0);CHECK(ackerror(nl)==-ESTALE);close(nl);
 pid_t child=fork();CHECK(child>=0);
 if(!child){close(client);close(peer);close(listener);if(setgroups(0,NULL)||setgid(2000)||setuid(2000))_exit(2);
  int f=nlopen();if(f<0)_exit(errno==EPERM||errno==EACCES?0:3);
  if(sendreq(f,DESTROY,family,0,&id)<0)_exit(errno==EPERM||errno==EACCES?0:4);
  _exit(ackerror(f)==-EPERM?0:5);
 }
 int status;CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
 nl=nlopen();CHECK(nl>=0);CHECK(sendreq(nl,DESTROY,family,0,&id)>0);CHECK(ackerror(nl)==0);close(nl);
 int error=0;socklen_t elen=sizeof(error);CHECK(getsockopt(client,SOL_SOCKET,SO_ERROR,&error,&elen)==0);CHECK(error==ECONNABORTED);
 close(client);close(peer);close(listener);
 printf("PASS IPv%d: transfer, TCP_INFO144 real counters out=%u in=%u bytes_received=%llu, diagnostic family/mark, stale cookie, denied unprivileged destroy, own-socket abort\n",family==AF_INET?4:6,out,in,bytes);
 return 0;
}
int main(void){CHECK(fixture(AF_INET)==0);CHECK(fixture(AF_INET6)==0);return 0;}
