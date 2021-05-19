// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
    if (argc < 2) {
        cprintf("missing directory path\n");
        exit();
    }

    int r;

    r = open(argv[1], O_CREAT | O_DIRECTORY | O_EXCL | O_RDWR);
    if (r < 0) {
        cprintf("unable to create directory: %e\n", r);
        exit();
    }
}
