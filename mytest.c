#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

char *buffer;
int length = 50000;
void setBuf(char c) {
    memset(buffer, c, length);
    buffer[length - 1] = '\0';
}

int main(int argc, char *argv[]) {

    
    buffer = malloc(length);
    int fd = open("/dev/asgn1", O_RDWR);
    int rc = 0;
    
    setBuf('x');
    printf("buffer value: %s\n", buffer);
    
    rc = write(fd, buffer, length);
    printf("file: %d write: %d\n", fd, rc);
    
    setBuf('y');
    
    printf("buffer now: %s\n", buffer);
    rc = write(fd, buffer, length);
    printf("file: %d write: %d\n", fd, rc);
    
    setBuf('z');
    printf("now %s\n", buffer);
    
    lseek(fd, 0, SEEK_SET);
    rc = read(fd, buffer, length);
    printf("buffer after read: %s\n", buffer);
    rc=read(fd,buffer,length);
    printf("buffer after read %s\n",buffer);
    
    close(fd);
    
    return 0;
}
