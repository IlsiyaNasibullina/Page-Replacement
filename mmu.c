#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <math.h>

struct PTE{
    // The page is in the physical memory (RAM)
    bool valid;
    // The frame number of the page in the RAM
    int frame;
    // The page should be written to disk
    bool dirty;
    // The page is referenced/requested
    int referenced;
    int counter;
    unsigned char counter_ageing;
};
struct PTE *page_table_struct;

void handler(int signum){
//    sleep(1);
    printf("MMU resumed by SIGCONT signal from pager\n");
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        perror("Error in args in mmu.c\n");
        exit(EXIT_FAILURE);
    }
    int num_pages = atoi(argv[1]);
    pid_t pager_pid = atoi(argv[argc - 1]);

    if (mkdir("/tmp/ex2/", 0777) == -1 && errno != EEXIST) {
        perror("Error while creating directory /tmp/ex2/ in pager.c\n");
        exit(EXIT_FAILURE);
    }

    int page_table = open("/tmp/ex2/pagetable", O_RDWR);
    size_t page_table_size = sizeof(struct PTE) * num_pages;
    page_table_struct = mmap(NULL, page_table_size, PROT_READ | PROT_WRITE, MAP_SHARED, page_table, 0);
    int hit = 0;
    int miss = 0;

    signal(SIGCONT, handler);
    for (int i = 2; i < argc - 1;) {
        //total++;
        char mode = argv[i][0];
        if (mode == 'W' || mode == 'R') {
            pid_t pid = getpid();
            int page_number = atoi(&argv[i][1]);
            if (mode == 'R'){
                printf("Read request for page %d\n", page_number);
            }
            if (mode == 'W') {
                printf("Write request for page %d\n", page_number);
            }

            if (page_number < 0 || page_number > num_pages) {
                perror("Invalid input in args in mmu.c\n");
                exit(EXIT_FAILURE);
            }
            page_table_struct[page_number].counter++;
            for (int j = 0; j < num_pages; j++) {
                page_table_struct[j].counter_ageing = page_table_struct[j].counter_ageing / 2;
            }
            page_table_struct[page_number].counter_ageing += pow(2, sizeof(unsigned char) * 8 - 1);

            if (!page_table_struct[page_number].valid) {
                miss++;
                printf("It is not a valid page ---> page fault\n");
                page_table_struct[page_number].referenced = pid;
                printf("Ask pager to load it from disk (SIGUSR1 signal) and wait\n");
                kill(pager_pid, SIGUSR1);
                pause();
            }
            else {
                page_table_struct[page_number].counter++;
                for (int j = 0; j < num_pages; j++) {
                    page_table_struct[j].counter_ageing = page_table_struct[j].counter_ageing / 2;
                }
                page_table_struct[page_number].counter_ageing += pow(2, sizeof(unsigned char) * 8 - 1);
                hit++;
                printf("It is a valid page!\n");
            }

            if(mode == 'W') {
                printf("It is a write request then set the dirty field\n");
                page_table_struct[page_number].dirty = true;
            }
            printf("MMU resumed by SIGCONT signal from pager\n");
            printf("Page table\n");
            for (int j = 0; j < num_pages; j++) {
                printf("Page %d ---> valid=%d, frame=%d, dirty=%d, referenced=%d\n", j, page_table_struct[j].valid,
                       page_table_struct[j].frame, page_table_struct[j].dirty, page_table_struct[j].referenced);
            }
            i++;
        }
        else {
            i++;
        }
    }


    printf("The hit ratio is: %.2f%%\n", ((double)hit / (hit + miss)) * 100.0);

    printf("Done all requests.\n");
    printf("MMU sends SIGUSR1 to the pager.\n");
    printf("MMU terminates.\n");
    kill(pager_pid, SIGUSR1);
    munmap(page_table_struct, page_table_size);
    close(page_table);
    return EXIT_SUCCESS;
}
