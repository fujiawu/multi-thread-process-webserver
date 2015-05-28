#ifndef SHM_CHANNEL_H
#define SHM_CHANNEL_H

#define MAX_CACHE_REQUEST_LEN 1024

/* helper struct for message queue */
struct request_msg {
    long mtype;
    int shmid;         // which shared mem to use
    char mdata[MAX_CACHE_REQUEST_LEN];
};

/* helper struct for shm */
struct shm_data_struct {
    pthread_mutex_t mutex;
    pthread_cond_t server;  // server wait
    pthread_cond_t cache;   // cache wait
    int size;          // file size
    // if size = -2: cache is processing the request
    // if size = -1: file not found, data transfer finished
    // if size >= 0: file found
    int byte;          // current data size
    // if byte = 0: cache is writing
    // if byte > 0: cache has done writing in this round
};

#endif
