#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define GB (1 << 30)

int main(int argc, char *argv[])
{
        int size = 1; // 1GB Default

        if (argc > 1)
                size = atoi(argv[1]);

        unsigned char *ptr[size];

        printf("Starting allocation of %d GB [PID: %d]\n", size, getpid());

        for (int i = 0; i < size; i++)
        {
                ptr[i] = mmap(NULL, GB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (ptr == MAP_FAILED)
                {
                        printf("mmap error");
                        return EXIT_FAILURE;
                }
                unsigned char *p = ptr[i];
                p[0] = 's';
                p[GB - 1] = 'e';
                memset(p, '*', GB);
                printf("[%d] memset at %p\n", i + 1, p);
        }
        
        printf("Press any key to free memory");
        getchar();

        for (int i = 0; i < size; i++)
        {
                if (ptr[i] != NULL)
                {
                        munmap(ptr[i], GB);
                }
        }
        
        printf("Press any key to exit");
        getchar();
        return EXIT_SUCCESS;
}