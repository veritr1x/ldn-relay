/* MIT. Used on LRTransport's serial queue; USB bridge listens on loopback only. */
- (void)usbFail:(NSString *)reason {
    if(usbSocket>=0)close(usbSocket);usbSocket=-1;usbStopped=YES;[self reset];
    [self status:reason];
}
- (void)usbTick {
    if(usbStopped)return;
    if(usbSocket<0){
        if(clockMs()<usbRetry)return;usbRetry=clockMs()+1000;
        int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return;
        /* Connecting to loopback fails immediately while helper is absent. */
        struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(LU_PORT),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
        if(connect(fd,(struct sockaddr *)&a,sizeof(a))){close(fd);return;}
        int one=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
        fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);usbSocket=fd;nonce=1;limit=LR_MAX_FRAME;usbInput=[NSMutableData new];usbOutput=[NSMutableData new];
        [self status:@"USB companion connected to local cable bridge; waiting for Switch capabilities."];
    }
    while(outgoing.count && usbOutput.length<8*LU_PAGE){
        NSData *d=outgoing[0];uint8_t page[LU_PAGE];if(!lu_pack(page,d.bytes,d.length)){[self usbFail:@"Invalid USB output message."];return;}
        [usbOutput appendBytes:page length:LU_PAGE];[outgoing removeObjectAtIndex:0];
    }
    if(usbOutput.length){ssize_t n=send(usbSocket,usbOutput.bytes,usbOutput.length,0);
        if(n>0)[usbOutput replaceBytesInRange:NSMakeRange(0,n) withBytes:NULL length:0];
        else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR){[self usbFail:@"USB bridge disconnected. Restart USB relay and companion."];return;}
    }
    for(unsigned i=0;i<32 && usbInput.length<8*LU_PAGE;i++){
        uint8_t bytes[LU_PAGE];ssize_t n=recv(usbSocket,bytes,sizeof(bytes),0);
        if(n>0)[usbInput appendBytes:bytes length:n];else {if(n==0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR))[self usbFail:@"USB bridge disconnected. Restart USB relay and companion."];break;}
    }
    while(!usbStopped && usbInput.length>=LU_PAGE){const uint8_t *p;size_t n;
        if(!lu_unpack(usbInput.bytes,LU_PAGE,&p,&n)){[self usbFail:@"Invalid USB page; reconnect required."];return;}
        if(![self receive:[NSData dataWithBytes:p length:n]])break;
        [usbInput replaceBytesInRange:NSMakeRange(0,LU_PAGE) withBytes:NULL length:0];
    }
}
