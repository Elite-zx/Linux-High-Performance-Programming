#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  if (argc <= 3) {
    /*basename function: Get the filename portion of the path string.
     * This function is in <libgen.h> header file*/
    printf("usage: %s ip_address port_number filename\n", basename(argv[0]));
    return 1;
  }
  const char* ip = argv[1];
  /*convert  ASCII (string) to integer*/
  int port = atoi(argv[2]);
  const char* file_name = argv[3];

  /*file status information, including file type, size, permissions*/
  int filefd = open(file_name, O_RDONLY);
  assert(filefd > 0);
  struct stat stat_buf;
  fstat(filefd, &stat_buf);

  struct sockaddr_in address;
  /*Clear the specified memory area to zero*/
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  /*convert ipv4 dotted decimal address to integer ip address*/
  inet_pton(AF_INET, ip, &address.sin_addr);
  /*convert host byte order to network byte order (big-endian)*/
  address.sin_port = htons(port);

  int sock = socket(PF_INET, SOCK_STREAM, 0);
  assert(sock >= 0);

  int ret = bind(sock, (struct sockaddr*)&address, sizeof(address));
  assert(ret != -1);

  ret = listen(sock, 5);
  assert(ret != -1);

  struct sockaddr_in client;
  socklen_t client_addrlength = sizeof(client);
  int connfd = accept(sock, (struct sockaddr*)&client, &client_addrlength);
  if (connfd < 0) {
    printf("errno is: %d\n", errno);
  } else {
    /*Write the contents of a file to a socket without going through userspace*/
    sendfile(connfd, filefd, NULL, stat_buf.st_size);
    close(connfd);
  }

  close(sock);
  return 0;
}

