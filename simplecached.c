#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

#include "shm_channel.h"
#include "simplecache.h"
#include "steque.h"

static void
_sig_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        /* Unlink IPC mechanisms here */
        exit(signo);
    }
}

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  simplecached [options]\n"                                                  \
"options:\n"                                                                  \
"  -t [thread_count]   Num worker threads (Default: 1, Range: 1-1000)\n"      \
"  -c [cachedir]       Path to static files (Default: ./)\n"                  \
"  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"nthreads", required_argument, NULL, 't'},
    {"cachedir", required_argument, NULL, 'c'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

/* task struct */
struct _task_t {
    char path[MAX_CACHE_REQUEST_LEN];
    int shmid;
};
typedef struct _task_t task_t;

/* task queue */
static steque_t taskq;

/* internal synchronization */
static pthread_mutex_t taskqlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t taskqempty = PTHREAD_COND_INITIALIZER;

void
Usage()
{
    fprintf(stdout, "%s", USAGE);
}

/* cache worker function */
void *
worker_main(void *arg)
{
    while (1) {

        //printf("=============\n");

        /* get a task from queue */
        pthread_mutex_lock(&taskqlock);
        while (steque_isempty(&taskq))
            pthread_cond_wait(&taskqempty, &taskqlock);
        task_t *task = steque_pop(&taskq);
        pthread_mutex_unlock(&taskqlock);

        /* map shm */
        struct shm_data_struct *shm_data = (struct shm_data_struct *)
            shmat(task->shmid, (void *) 0, 0);
        if ((char *) shm_data == (char *) (-1)) {
            fprintf(stderr, "could not map shared memory\n");
            free(task);
            continue;
        }

        /* get segment size */
        struct shmid_ds shminfo;
        shmctl(task->shmid, IPC_STAT, &shminfo);
        size_t segment_size = shminfo.shm_segsz;
        if (segment_size < sizeof(struct shm_data_struct)) {
            fprintf(stderr, "segment size too small\n");
        }
        size_t buffer_len = segment_size - sizeof(struct shm_data_struct);

        /* get cache fd */
        //printf("got file:%s\n", task->path);
        int fd = simplecache_get(task->path);
        //printf("fd:%i\n", fd);

        free(task);

        /* write filesize to shm */
        int file_len;
        if (fd < 0) {                  // file doesn't exist in cache
            file_len = -1;
        } else {
            file_len = lseek(fd, 0, SEEK_END);
            lseek(fd, 0, SEEK_SET);
        }
        pthread_mutex_lock(&shm_data->mutex);
        shm_data->size = file_len;
        //printf("filesize:%i\n", shm_data->size);
        pthread_cond_signal(&shm_data->server);
        pthread_mutex_unlock(&shm_data->mutex);

        /* send file data */
        if (shm_data->size != -1) {
            size_t sent = 0;
            size_t write_len;
            char *buffer = (char *) shm_data + sizeof(struct shm_data_struct);
            while (sent < shm_data->size) {
                pthread_mutex_lock(&shm_data->mutex);
                while (shm_data->byte != 0)
                    pthread_cond_wait(&shm_data->cache, &shm_data->mutex);
                write_len = read(fd, buffer, buffer_len);
                if (write_len <= 0)
                    fprintf(stderr, "read file error\n");
                sent += write_len;
                shm_data->byte = write_len;
                pthread_cond_signal(&shm_data->server);
                pthread_mutex_unlock(&shm_data->mutex);
            }
        }

        /* detach shm */
        if (shmdt(shm_data) == -1)
            fprintf(stderr, "could not detach shared memory\n");

    }
    pthread_exit(0);
}

/* cache main function */
int
main(int argc, char **argv)
{
    int nthreads = 1;
    char *cachedir = "locals.txt";
    char option_char;

    while ((option_char = getopt_long(argc, argv, "t:c:h",
                                      gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 't':                     // thread-count
            nthreads = atoi(optarg);
            break;
        case 'c':                     //cache directory
            cachedir = optarg;
            break;
        case 'h':                     // help
            Usage();
            exit(0);
            break;
        default:
            Usage();
            exit(1);
        }
    }

    if (signal(SIGINT, _sig_handler) == SIG_ERR) {
        fprintf(stderr, "Can't catch SIGINT...exiting.\n");
        exit(EXIT_FAILURE);
    }

    if (signal(SIGTERM, _sig_handler) == SIG_ERR) {
        fprintf(stderr, "Can't catch SIGTERM...exiting.\n");
        exit(EXIT_FAILURE);
    }

    /* Initializing the cache */
    simplecache_init(cachedir);

    /* create worker threads */
    pthread_t worker[nthreads];
    int workerID[nthreads];
    int i = 0;
    for (i = 0; i < nthreads; i++) {
        workerID[i] = i;
        if (0 != pthread_create(&worker[i], NULL, worker_main, workerID + i)) {
            fprintf(stderr, "could not create worker thread %i\n", workerID[i]);
            break;
        }
    }
    int threads_created = i;

    /* init the request queue */
    steque_init(&taskq);

    /* get key for message queue */
    key_t key = ftok("webserver", 'z');

    /* start accepting request */
    while (1) {
        int msq = msgget(key, 0600);
        if (msq == -1) {
            fprintf(stderr, "could not find message queue\n"
                    "trying again ...\n");
            sleep(2);
            continue;
        }
        struct request_msg msgbuf;
        while (1) {
            if (msgrcv(msq, &msgbuf, sizeof(msgbuf)
                       - sizeof(long), 1, 0) == -1) {
                fprintf(stderr, "could not receive message\n"
                        "trying again ...\n");
                break;
            }

            /* add task to queue */
            task_t *task = malloc(sizeof(task_t));
            task->shmid = msgbuf.shmid;
            strcpy(task->path, msgbuf.mdata);
            pthread_mutex_lock(&taskqlock);
            steque_enqueue(&taskq, task);
            pthread_cond_signal(&taskqempty);
            pthread_mutex_unlock(&taskqlock);
        }
    }

    /* join all worker threads */
    for (int i = 0; i < threads_created; i++) {
        if (0 != pthread_join(worker[i], NULL)) {
            fprintf(stderr, "could not join worker thread #%i\n", workerID[i]);
        }
    }

}
