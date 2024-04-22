#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

// I learned how to create a structure from https://www.tutorialspoint.com/cprogramming/c_structures.htm
struct Encoded{
    unsigned char *arr;
    int count;
    int id;
    int finished;
};

struct Argument{
    char* file;
    int start;
    int end;
    int id;
};

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t not_empty1 = PTHREAD_COND_INITIALIZER;
pthread_cond_t not_empty2 = PTHREAD_COND_INITIALIZER;
struct Argument TaskQueue[250000];
int taskCount = 0;
struct Encoded CompletedTaskQueue[250000];
int completedTaskCount = 0;
//struct Encoded *CollectedTaskQueue;
int collectedTaskCount = 0;
int id = 0;
bool produced = false;
int needSignal = 0;

struct Encoded converter(struct Argument *Args){
    struct Argument *File = (struct Argument *)Args;
    char *str = File->file;
    int start = File->start;
    int end = File->end;

    struct Encoded Result;
    int i = start;
    unsigned char prev = 0;
    int prevCount = 0;
    Result.arr = (unsigned char*)malloc((end - start + 1) * sizeof(unsigned char));
    Result.count = 0;
    Result.id = File->id;

    // I learned how to loop through string from https://dev.to/zirkelc/how-to-iterate-over-c-string-lcj
    // I learned that int can be compared with size_t from https://stackoverflow.com/questions/3642010/can-i-compare-int-with-size-t-directly-in-c
    while(i <= end){
        if(prevCount == 0){
            prev = (unsigned char)str[i];
            prevCount = 1;
            i++;
        }else{
            // I learned how to get a substring from https://www.geeksforgeeks.org/get-a-substring-in-c/
            unsigned char letter;
            letter = (unsigned char)str[i];
            if(prev == letter){
                prevCount++;
            }else{
                Result.arr[Result.count] = prev;
                Result.count++;
                // I learned how to convert int to 1-byte unsigned char from https://stackoverflow.com/questions/10319805/c-unsigned-int-to-unsigned-char-array-conversion
                Result.arr[Result.count] = (unsigned char)(prevCount & 0xFF);
                Result.count++;

                prev = (unsigned char)str[i];
                prevCount = 1;
            }
            i++;
        }
    }
    Result.arr[Result.count] = prev;
    Result.count++;
    // I learned how to convert int to string from https://stackoverflow.com/questions/10162465/implicit-declaration-of-function-itoa-is-invalid-in-c99
    Result.arr[Result.count] = (unsigned char)(prevCount & 0xFF);
    Result.count++;
    Result.finished = 1;
    return Result;
}

void producer(char *addr, int size){
    int remainder = size % 4096;
    int quotient = size / 4096;

    pthread_mutex_lock(&mutex1);
    for(int i = 0; i < quotient; i++){
        struct Argument Segment;
        Segment.file = addr;
        Segment.start = i * 4096;
        Segment.end = (i + 1) * 4096 - 1;
        Segment.id = id;
        id++;
        TaskQueue[taskCount] = Segment; // also increments count
        taskCount++;
    }
    if(remainder != 0){
        struct Argument Segment;
        Segment.file = addr;
        Segment.start = quotient * 4096;
        Segment.end = size - 1;
        Segment.id = id;
        id++;
        TaskQueue[taskCount] = Segment; // also increments count
        taskCount++;
    }

    pthread_cond_signal(&not_empty1);
    pthread_mutex_unlock(&mutex1);
}

// I learned how to define the header for thread-starting program from https://man7.org/linux/man-pages/man3/pthread_create.3.html
static void *consumer(
    //void * i
    ) {
    struct Argument Args;
    //int j = *((int*) i );
    //printf("Thread %d created\n", j);
    while(1){
        pthread_mutex_lock(&mutex1);
        while (taskCount == 0){
            needSignal++;
            pthread_cond_wait(&not_empty1, &mutex1);
            needSignal--;
            if(produced && taskCount == 0){
                //printf("Thread %d terminated for mutex1\n", j);
                pthread_mutex_unlock(&mutex1);
                pthread_exit(NULL);
            }
        }
        Args = TaskQueue[taskCount - 1]; // also decrements count
        taskCount--;
        pthread_mutex_unlock(&mutex1);
        struct Encoded Item = converter(&Args);
        //printf("Thread %d waiting for mutex2\n", j);
        pthread_mutex_lock(&mutex2);
        CompletedTaskQueue[Item.id] = Item;
        completedTaskCount++;
        if(Item.id == collectedTaskCount){
            //printf("Signal sent for item %d\n", Item.id);
            pthread_cond_signal(&not_empty2);
        }
        pthread_mutex_unlock(&mutex2);
        //printf("Thread %d passed for mutex2\n", j);
        pthread_mutex_lock(&mutex1);
        if(produced && taskCount == 0){
            //printf("Thread %d terminated for mutex1\n", j);
            pthread_mutex_unlock(&mutex1);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&mutex1);
    }
    pthread_exit(NULL);
}


struct Encoded completed(struct Encoded Prev){
    while(1){
        pthread_mutex_lock(&mutex2);
        //printf("Waiting for item %d\n", collectedTaskCount);
        if(collectedTaskCount >= id) break;
        while (completedTaskCount == 0 || 
        CompletedTaskQueue[collectedTaskCount].id != collectedTaskCount || 
        CompletedTaskQueue[collectedTaskCount].finished != 1){
            pthread_cond_wait(&not_empty2, &mutex2);
        }
        struct Encoded Result = CompletedTaskQueue[collectedTaskCount];
        //printf("Collected item %d\n", collectedTaskCount);
        pthread_mutex_unlock(&mutex2);
        //write(1, "Task starts here", 17);
        // I learned how to check unitialized struct from https://stackoverflow.com/questions/35387229/how-can-i-check-if-a-variable-in-a-struct-is-not-used-in-c
        if(Result.count > 0){
            //write(1, "Enter0", 7);
            if(Prev.count != 0 && Prev.arr[0] == Result.arr[0] && Result.count == 2){
                //write(1, "Enter1", 7);
                Prev.arr[0] = Result.arr[0];
                int add1 = Result.arr[1];
                int add2 = Prev.arr[1];
                Prev.arr[1] = (unsigned char)((add1 + add2) & 0xFF);
                Prev.count = 2;
            }
            else if(Prev.count != 0 && Prev.arr[0] == Result.arr[0] && Result.count > 2){
                //write(1, "Enter2", 7);
                write(1, &Prev.arr[0], 1);
                int add1 = Result.arr[1];
                int add2 = Prev.arr[1];
                unsigned char sum = (unsigned char)((add1 + add2) & 0xFF);
                write(1, &sum, 1);

                write(1, Result.arr + 2, Result.count - 4);
                Prev.arr[0] = Result.arr[Result.count - 2];
                Prev.arr[1] = Result.arr[Result.count - 1];
                Prev.count = 2;
            }else if(Prev.count != 0){
                //write(1, "Enter3", 7);
                write(1, &Prev.arr[0], 2);
                write(1, Result.arr, Result.count - 2);

                Prev.arr[0] = Result.arr[Result.count - 2];
                Prev.arr[1] = Result.arr[Result.count - 1];
                Prev.count = 2;
            }else if(Prev.count == 0){
                //write(1, "Enter4", 7);
                write(1, Result.arr, Result.count - 2);

                Prev.arr[0] = Result.arr[Result.count - 2];
                Prev.arr[1] = Result.arr[Result.count - 1];
                Prev.count = 2;
            }
        }
        pthread_mutex_lock(&mutex2);
        collectedTaskCount++;
        if(collectedTaskCount >= id) break;
        pthread_mutex_unlock(&mutex2);
        //printf("Proceeding to item %d\n", collectedTaskCount);
        //free(Result.arr);
    }
    return Prev;
}

void parser(int argc, char *argv[]){
    // the arrows are automatically parsed by the command line. We only need to parse the flags.
    int c, index;
    extern int optind, optopt;
    int threads = 0;
    pthread_t *threadIds;
    // I learned how to parse arguments from https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
    while ((c = getopt (argc, argv, "j:")) != -1){
        switch (c)
        {
            case 'j':
                // I learned to convert int from string from https://blog.udemy.com/c-string-to-int/#:~:text=The%20atoi()%20function%20converts,type%20pointer%20to%20a%20character.
                threads = atoi(optarg);
                // I learned how to create threads from https://en.wikipedia.org/wiki/Pthreads
                if(threads > 0){
                    threadIds = (pthread_t *)malloc(threads * sizeof(pthread_t));
                    for (int i = 0; i < threads; i++) {
                        //printf("IN MAIN: Creating thread %d.\n", i);
                        pthread_create(&threadIds[i], NULL, consumer,  (void*)& i);
                    }
                }
                break;
            case '?':
                fprintf (stderr, "Option -%c is not supported.\n", optopt);
                break;
            default:
                printf ("Failed.\n");
                exit(1);
                break;
        }
    } 

    struct Encoded Prev;
    Prev.arr = (unsigned char*)malloc(2 * sizeof(unsigned char));
    Prev.count = 0;
    // I learned how to print arguments from https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
    for (index = optind; index < argc; index++){

        //I learned how to use mmap() from the lab hints
        int fd = open(argv[index], O_RDONLY);
        if (fd == -1) fprintf (stderr, "Failed in initiating the file descriptor");

        // Get file size
        struct stat sb;
        if (fstat(fd, &sb) == -1) fprintf (stderr, "Failed in getting the file size");

        // Map file into memory
        char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED) fprintf (stderr, "Failed in mapping file into memory");
        else if (threads > 0){
            // I learn about whole division and modulo operaitons in c from https://scalettar.physics.ucdavis.edu/cosmos/modulo.pdf
            producer(addr, (int)sb.st_size);
        }
        else{
            struct Argument Segment;
            Segment.file = addr;
            Segment.start = 0;
            Segment.end = (int)sb.st_size - 1;
            Segment.id = id;
            id++;
            struct Encoded Result = converter(&Segment);
            //write(1, "File starts here", 17);
            // I learned how to check unitialized struct from https://stackoverflow.com/questions/35387229/how-can-i-check-if-a-variable-in-a-struct-is-not-used-in-c
            if(Result.count > 0){
                if(Prev.count != 0 && Prev.arr[0] == Result.arr[0] && Result.count == 2){
                    Prev.arr[0] = Result.arr[0];
                    int add1 = Result.arr[1];
                    int add2 = Prev.arr[1];
                    Prev.arr[1] = (unsigned char)((add1 + add2) & 0xFF);
                    Prev.count = 2;
                }
                else if(Prev.count != 0 && Prev.arr[0] == Result.arr[0] && Result.count > 2){
                    write(1, &Prev.arr[0], 1);
                    int add1 = Result.arr[1];
                    int add2 = Prev.arr[1];
                    unsigned char sum = (unsigned char)((add1 + add2) & 0xFF);
                    write(1, &sum, 1);

                    write(1, Result.arr + 2, Result.count - 4);
                    Prev.arr[0] = Result.arr[Result.count - 2];
                    Prev.arr[1] = Result.arr[Result.count - 1];
                    Prev.count = 2;
                }else if(Prev.count != 0){
                    write(1, &Prev.arr[0], 2);
                    write(1, Result.arr, Result.count - 2);

                    Prev.arr[0] = Result.arr[Result.count - 2];
                    Prev.arr[1] = Result.arr[Result.count - 1];
                    Prev.count = 2;
                }else if(Prev.count == 0){
                    write(1, Result.arr, Result.count - 2);

                    Prev.arr[0] = Result.arr[Result.count - 2];
                    Prev.arr[1] = Result.arr[Result.count - 1];
                    Prev.count = 2;
                }
            }
            //free(Result.arr);
        }
    }
    if(threads > 0){
        pthread_mutex_lock(&mutex1);
        produced = true;
        pthread_mutex_unlock(&mutex1);
        Prev = completed(Prev);
        for(int i = 0; i < threads; i++) {
            // I learned how to check the mutex from https://stackoverflow.com/questions/24854903/pthread-cond-wait-not-waked-up-correctly-if-not-joined
            pthread_mutex_lock(&mutex1);
            if(needSignal){
                //printf("send signal to %d.\n", i);
                pthread_cond_signal(&not_empty1); 
            }
            pthread_mutex_unlock(&mutex1);
            pthread_join(threadIds[i], NULL);
            //printf("IN MAIN: Thread %d has ended.\n", i);
        }
            //free(Result.arr);
    }

    if(Prev.count != 0){
        write(1, &Prev.arr[0], 2);
    }
    //free(Prev.arr);
    //free(threadIds);
}

int main(int argc, char *argv[]){
    parser(argc, argv);
    return 0;
}