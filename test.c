#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

char *buffer;
int length = 50;
void setBuf(char c) {
    memset(buffer, c, length);
    buffer[length - 1] = '\0';
}

int main(int argc, char *argv[]) {
    
    buffer = malloc(length);
    int fd = open("/dev/asgn1", O_RDWR);
    int rc = 0;
    
    setBuf('x');
    printf("===test write\n");
    printf("Set buffer to: \n%s\n", buffer);
    
    rc = write(fd, buffer, length);
    printf("After write buffer into file...\n");
    //printf("file: %d write: %d\n", fd, rc);
    
    printf("===test read\n");
    printf("set buffer to Y\n");
    setBuf('Y');
    printf("the buffer now is:\n");
    printf("%s\n", buffer);
    printf("first, seek to 0 position of the file\n");
    lseek(fd, 10, SEEK_SET);

    printf("second, read out the file's content");
    rc = read(fd, buffer, length);
    printf("succeed in reading %d amout of data out of file into buffer.\n", rc);
    printf("now, buffer changed to \n%s\n", buffer);

    /*
    setBuf('y');
    
    printf("buffer now: \n%s\n", buffer);
    rc = write(fd, buffer, length);
    printf("file: %d write: %d\n", fd, rc);
    
    setBuf('z');
    printf("now \n%s\n", buffer);
    
    lseek(fd, 10, SEEK_SET);
    rc = read(fd, buffer, length);
    printf("buffer after read: %s\n", buffer);
    rc=read(fd,buffer,length);
    printf("buffer after read %s\n",buffer);
    */
    close(fd);
    
    return 0;
}
