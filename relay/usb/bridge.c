/* MIT. One local companion, one libnx USB interface, no LAN listener. */
#include <libusb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include "relay_usb.h"
static volatile sig_atomic_t stopping;
static void stop(int s){(void)s;stopping=1;}
static bool full_send(int fd,const uint8_t *p,size_t n){while(n){ssize_t k=send(fd,p,n,0);if(k<0 && errno==EINTR)continue;if(k<=0)return false;p+=k;n-=(size_t)k;}return true;}
int main(void){
 signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGPIPE,SIG_IGN);setvbuf(stdout,NULL,_IOLBF,0);
 libusb_context *ctx=NULL;if(libusb_init(&ctx))return 1;
 libusb_device_handle *usb=NULL;puts("Waiting for LDN Relay USB mode (press X on Switch).");
 while(!stopping && !usb){usb=libusb_open_device_with_vid_pid(ctx,0x057e,0x3000);if(!usb)usleep(100000);}
 if(!usb){libusb_exit(ctx);return 1;}
 struct libusb_config_descriptor *cfg=NULL;int iface=-1;unsigned char in=0,out=0;
 if(libusb_get_active_config_descriptor(libusb_get_device(usb),&cfg))goto fail;
 for(unsigned i=0;i<cfg->bNumInterfaces;i++)for(int a=0;a<cfg->interface[i].num_altsetting;a++){
  const struct libusb_interface_descriptor *d=&cfg->interface[i].altsetting[a];
  if(d->bInterfaceClass!=255 || d->bInterfaceSubClass!=255 || d->bInterfaceProtocol!=255)continue;
  unsigned char ei=0,eo=0;for(unsigned e=0;e<d->bNumEndpoints;e++){const struct libusb_endpoint_descriptor *ep=&d->endpoint[e];if((ep->bmAttributes&3)!=LIBUSB_TRANSFER_TYPE_BULK)continue;if(ep->bEndpointAddress&128)ei=ep->bEndpointAddress;else eo=ep->bEndpointAddress;}
  if(ei&&eo){iface=d->bInterfaceNumber;in=ei;out=eo;break;}
 }
 libusb_free_config_descriptor(cfg);cfg=NULL;
 if(iface<0 || libusb_claim_interface(usb,iface)){puts("Cannot claim USB relay interface.");goto fail;}
 printf("USB claimed interface=%d in=%02x out=%02x speed=%d\n",iface,in,out,libusb_get_device_speed(libusb_get_device(usb)));
 int server=socket(AF_INET,SOCK_STREAM,0),yes=1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
 struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(LU_PORT),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
 if(server<0 || bind(server,(struct sockaddr *)&addr,sizeof(addr)) || listen(server,1)){perror("loopback listener");if(server>=0)close(server);goto release;}
 printf("Listening on 127.0.0.1:%d; open USB game companion.\n",LU_PORT);
 struct pollfd waiting={.fd=server,.events=POLLIN};
 while(!stopping){int ready=poll(&waiting,1,100);if(ready>0)break;if(ready<0 && errno!=EINTR){stopping=1;break;}}
 int client=stopping?-1:accept(server,NULL,NULL);close(server);if(client<0)goto release;
 struct timeval deadline={.tv_sec=2};setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&deadline,sizeof(deadline));setsockopt(client,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
 puts("Companion connected.");uint8_t tx[LU_PAGE],rx[LU_PAGE];size_t used=0,rxused=0;unsigned long sent=0,got=0;
 while(!stopping){
  ssize_t n=recv(client,tx+used,LU_PAGE-used,MSG_DONTWAIT);
  if(n==0)break;if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)break;if(n>0)used+=(size_t)n;
  if(used==LU_PAGE){const uint8_t *p;size_t len;if(!lu_unpack(tx,used,&p,&len)){puts("Invalid companion page.");break;}int moved=0,rc=libusb_bulk_transfer(usb,out,tx,LU_PAGE,&moved,1000);if(rc || moved!=LU_PAGE){printf("USB write failed rc=%d bytes=%d\n",rc,moved);break;}used=0;sent++;}
  int moved=0,rc=libusb_bulk_transfer(usb,in,rx+rxused,(int)(LU_PAGE-rxused),&moved,2);
  if(moved>0)rxused+=(size_t)moved;
  if(rc && rc!=LIBUSB_ERROR_TIMEOUT){printf("USB read failed rc=%d\n",rc);break;}
  if(rxused==LU_PAGE){const uint8_t *p;size_t len;if(!lu_unpack(rx,rxused,&p,&len)||!full_send(client,rx,rxused))break;rxused=0;got++;}
 }
 printf("USB bridge stopped: toSwitch=%lu fromSwitch=%lu. Restart Switch USB mode before reconnecting.\n",sent,got);close(client);
release:libusb_release_interface(usb,iface);
fail:if(cfg)libusb_free_config_descriptor(cfg);libusb_close(usb);libusb_exit(ctx);return 0;
}
