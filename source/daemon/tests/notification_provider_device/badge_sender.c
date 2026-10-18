/* Explicit isolated candidate badge command, never the production socket.
 * Success requires the provider's exact revision/count ACK, not just sendto. */
#include "../../strappy_notifications.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv)
{
  const char *path=STRAPPY_NOTIFICATION_DIRECTORY "/provider-candidate.sock";
  strappy_badge_packet packet,ack;
  struct sockaddr_un remote,local;
  int fd,result=1; fd_set readers; struct timeval timeout={3,0};
  struct stat info;
  if (argc!=3 || !*argv[1] || strlen(argv[1])>9 ||
      strspn(argv[1],"0123456789")!=strlen(argv[1]) || argv[1][0]=='0' ||
      (strcmp(argv[2],"8") && strcmp(argv[2],"0"))) return 2;
  if (lstat(STRAPPY_NOTIFICATION_DIRECTORY,&info) || !S_ISDIR(info.st_mode) ||
      info.st_uid!=geteuid() || (info.st_mode&077)) return 1;
  memset(&remote,0,sizeof(remote)); remote.sun_family=AF_UNIX; strcpy(remote.sun_path,path);
  memset(&local,0,sizeof(local)); local.sun_family=AF_UNIX;
  if (snprintf(local.sun_path,sizeof(local.sun_path),"%s.ack",path)>=(int)sizeof(local.sun_path)) return 1;
  fd=socket(AF_UNIX,SOCK_DGRAM,0); if(fd<0) return 1;
  if (bind(fd,(struct sockaddr *)&local,sizeof(local))) { close(fd); return 1; }
  if (chmod(local.sun_path,0600)) goto done;
  memset(&packet,0,sizeof(packet)); memcpy(packet.magic,"STB1",4);
  strcpy(packet.revision,argv[1]); strcpy(packet.count,argv[2]);
  if (sendto(fd,&packet,sizeof(packet),0,(struct sockaddr *)&remote,sizeof(remote))!=sizeof(packet)) goto done;
  FD_ZERO(&readers); FD_SET(fd,&readers);
  if (select(fd+1,&readers,NULL,NULL,&timeout)<=0) goto done;
  if (recv(fd,&ack,sizeof(ack),0)!=sizeof(ack) || memcmp(ack.magic,"STA1",4) ||
      memcmp(ack.revision,packet.revision,sizeof(packet.revision)) ||
      memcmp(ack.count,packet.count,sizeof(packet.count))) goto done;
  printf("Candidate acknowledged badge %s at revision %s.\n",argv[2],argv[1]); result=0;
done:
  if(result) fprintf(stderr,"No matching badge acknowledgment.\n");
  close(fd); unlink(local.sun_path); return result;
}
