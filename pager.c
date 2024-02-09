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
#include <limits.h>
#define PAGE_SIZE 8

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


int page_table;
int P;
int F;
int (*replacement_function)();

size_t page_table_size;
int disk_access = 0;

char **RAM;
char **disk;

int *counter;
unsigned char *counter_ageing;

void clean() {
    for (int i = 0; i < F; i++) {
        free(RAM[i]);
    }
    free(RAM);

    for (int i = 0; i < P; i++) {
        free(disk[i]);
    }
    free(disk);

    if (munmap(page_table_struct, page_table_size) == -1) {
        perror("Error while munmap in pager.c\n");
        exit(EXIT_FAILURE);
    }

    if (close(page_table) == -1) {
        perror("Error while closing page_table file in pager.c\n");
        exit(EXIT_FAILURE);
    }
}


// Random page replacement
int my_random() {
    return rand() % F;
}

// NFU page replacement
int nfu() {
    int min = INT_MAX;
    int number = 0;
    for (int i = 0; i < P; i++) {
        if (page_table_struct[i].valid == 1) {
            if (page_table_struct[i].counter < min) {
                min = page_table_struct[i].counter;
                number = i;
            }
        }
    }
    return number;
}

// Aging page replacement
int aging() {
    int min = INT_MAX;
    int number = 0;
    for (int i = 0; i < P; i++) {
        if (page_table_struct[i].valid == 1) {
            if (page_table_struct[i].counter_ageing < min) {
                min = page_table_struct[i].counter_ageing;
                number = i;
            }
        }
    }
    return number;

}


void handler(int signum) {
    sleep(0.5);
    pid_t pid = getpid();
    int victim_page = -1;
    int mmu_pid;
    for (int i = 0; i < P; i++) {
        if (page_table_struct[i].referenced != 0) {
            mmu_pid = page_table_struct[i].referenced;
            printf("A disk access request from MMU Process (pid=%d)\n", page_table_struct[i].referenced);
            victim_page = i;
            printf("Page %d is references\n", i);
            break;
        }
    }

    if (victim_page == -1) {
        printf("%d disk accesses in total\n", disk_access);
        printf("Pager is terminated\n");
        clean();
        kill(pid, SIGTERM);
        exit(EXIT_SUCCESS);
    }
    bool found_free = false;
    int j = -1;
    for (int i = 0; i < F; i++) {
        if (RAM[i] == NULL) {
            printf("We can allocate it to free frame %d\n", i);
            j = i;
            RAM[i] = malloc(PAGE_SIZE * sizeof(char));
            memcpy(RAM[j], disk[victim_page], PAGE_SIZE);
            disk_access++;
            printf("Copying data from disk (page=%d) to RAM (frame=%d)\n", victim_page, j);
            found_free = true;
            page_table_struct[victim_page].valid = true;
            page_table_struct[victim_page].frame = j;
            page_table_struct[victim_page].dirty = false;
            page_table_struct[victim_page].referenced = 0;
            break;
        }
    }
    if (!found_free) {
        printf("We do not have free frames in RAM\n");
        j = replacement_function();
      
        if (page_table_struct[j].dirty) {
            disk_access++;
            printf("Disc access is %d so far\n", disk_access);
            memcpy(disk[j], RAM[j], PAGE_SIZE);
            page_table_struct[j].dirty = false;
        }
        printf("Replace/Evict it with page %d to be allocated to frame %d\n", victim_page, j);
        memcpy(RAM[j], disk[victim_page], PAGE_SIZE);
        disk_access++;
        printf("Copying data from disk (page=%d) to RAM (frame=%d)\n", victim_page, j);
        page_table_struct[victim_page].referenced = 0;
        page_table_struct[victim_page].valid = true;
        page_table_struct[victim_page].frame = page_table_struct[j].frame;
        page_table_struct[victim_page].dirty = false;

        page_table_struct[j].valid = false;
        page_table_struct[j].frame = -1;
        page_table_struct[j].dirty = false;
        page_table_struct[j].referenced = 0;

    }

    printf("RAM array\n");
    for (int k = 0; k < F; k++) {
        printf("Frame %d ---> %s\n", k, RAM[k]);
    }

    kill(mmu_pid, SIGCONT);
    printf("Disk accesses is %d so far\n", disk_access);
    printf("Resume MMU process\n");
}


int main(int argc, char *argv[]) {
    if (argc < 4) {
        perror("Error in args in pager.c\n");
        exit(EXIT_FAILURE);
    }

    P = atoi(argv[1]);
    F = atoi(argv[2]);

    if (F < 0 || P < 0 ) {
        perror("Invalid number of pages or frames for pager.c\n");
        exit(EXIT_FAILURE);
    }

    RAM = malloc(F * sizeof(char *));
    disk = malloc(P * sizeof(char *));
    for (int i = 0; i < P; i++) {
        disk[i] = malloc(PAGE_SIZE * sizeof(char));
    }

    for (int i = 0; i < F; i++) {
        RAM[i] = NULL;
    }

    int rand = open("/dev/random", O_RDONLY);

    for (int i = 0; i < P;) {
        for (int j = 0; j < PAGE_SIZE;) {
            char c;
            if (read(rand, &c, 1) == 1) {
                if (isprint(c)) {
                    disk[i][j] = c;
                    j++;
                }
            }
        }
        i++;
    }
    close(rand);

    if (mkdir("/tmp/ex2/", 0777) == -1 && errno != EEXIST) {
        perror("Error while creating directory /tmp/ex2/ in pager.c\n");
        exit(EXIT_FAILURE);
    }


    page_table = open("/tmp/ex2/pagetable", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (page_table == -1) {
        perror("Error while opening /tmp/ex2/pagetable in pager.c\n");
        exit(EXIT_FAILURE);
    }
    page_table_size = sizeof(struct PTE) * P;
    ftruncate(page_table, page_table_size);
    page_table_struct = mmap(NULL, page_table_size, PROT_READ | PROT_WRITE, MAP_SHARED, page_table, 0);
    for (int i = 0; i < P; i++) {
        page_table_struct[i].valid = false;
        page_table_struct[i].frame = -1;
        page_table_struct[i].dirty = false;
        page_table_struct[i].referenced = 0;
        page_table_struct[i].counter = 0;
        page_table_struct[i].counter_ageing = 0;
    }
    printf("Initialized page table\n");
    for (int i = 0; i < P; i++) {
        printf("Page %d ---> valid=%d, frame=%d, dirty=%d, references=%d\n", i, page_table_struct[i].valid,
               page_table_struct[i].frame, page_table_struct[i].dirty, page_table_struct[i].referenced);
    }

    printf("Initialized RAM\n");
    for (int i = 0; i < F; i++) {
        printf("Frame %d ---> %s\n", i, RAM[i]);
    }
    printf("Initialized disk\n");
    for (int i = 0; i < P; i++) {
        printf("Page %d ---> %s\n", i, disk[i]);
    }

    signal(SIGUSR1, handler);

    char *algorithm = argv[3];
    printf("Selected page replacement algorithm: %s\n", algorithm);


    if (strcmp(algorithm, "random") == 0) {
        replacement_function = my_random;
    }
    else if (strcmp(algorithm, "nfu") == 0) {
        replacement_function = nfu;
    }
    else if (strcmp(algorithm, "aging") == 0) {
        replacement_function = aging;
    }
    else {
        perror("Invalid page replacement algorithm\n");
        exit(EXIT_FAILURE);
    }


    while(1) {
        pause();
    }
    return EXIT_SUCCESS;
}
