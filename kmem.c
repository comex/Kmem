#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

static uint64_t parse(const char *in) {
    bool hex = false;
    uint64_t result = 0;
    const char *s = in;
    if(s[0] == '0' && s[1] == 'x') {
        s += 2;
        hex = true;
    }
    if(!s[0]) goto bad;
    char c;
    while((c = *s++)) {
        result = result * (hex ? 16 : 10);
        if(c >= '0' && c <= '9') {
            result += (c - '0');
        } else if(c >= 'a' && c <= 'f') {
            result += 10 + (c - 'a');
        } else if(c >= 'A' && c <= 'F') {
            result += 10 + (c - 'A');
        } else {
            goto bad;
        }
    }
    return result;
bad:
    fprintf(stderr, "invalid number %s\n", in);
    exit(1);
}

int main(int argc, char **argv) {
    if(argc != 3) {
        fprintf(stderr, "usage: kmem addr size\n");
        return 1;
    }
    off_t addr = parse(argv[1]);
    size_t size = parse(argv[2]);
    int fd = open("/dev/kmem", O_RDONLY);
    if(fd == -1) {
        perror("open /dev/kmem");
        return 1;
    }
    void *buf = malloc(size);
    assert(buf);
    ssize_t ret = pread(fd, buf, size, addr);
    if(ret != size) {
        perror("pread");
        return 1;
    }
    write(1, buf, size);
    return 0;
}
