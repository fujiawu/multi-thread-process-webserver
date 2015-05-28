/* ===========================================================================
 * Introduction to Opertating Systems
 * CS 8803, CT 0MSCS
 *
 * Project 1: Multi-Threaded Webserver
 * Name: Fujia Wu
 * GTID: 902869936
 *
 * webclient.c
 * Implement the webclient
 *
==============================================================================*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <argp.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <regex.h>
#include <ctype.h>
#include <limits.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>

#include "steque.h"

#define BUFFER_SIZE 4096
#define FILE_NOT_FOUND "GetFile FILE_NOT_FOUND 0 "
#define FILE_FOUND "GetFile OK "
#define FILE_REQUEST "GetFile GET "
#define MAX_FILENAME_LENGTH 200

/* use GNU arg_parse library to parse arguments & options */

const char *argp_program_bug_address = "<fujiawu@gatech.edu>";

/* option specification */
static struct argp_option options[] = {
    {0, 's', "server", 0, "server address (Default: 0.0.0.0)"},
    {0, 'p', "port", 0, "port (Default: 8888)"},
    {0, 't', "thread", 0,
     "number of worker threads (Default: 1, Range: 1-1000)"},
    {0, 'w', "workload", 0, "path to workload file (Default: workload.txt)"},
    {0, 'd', "download", 0,
     "path to downloaded file directory (Default: null)"},
    {0, 'r', "request", 0,
     "number of total requests (Default: 10, Range: 1-1000"},
    {0, 'm', "metric", 0, "path to metrics file (Default: metrics.txt)"},
    {0, 'h', 0, 0, "show help message"},
    {0}
};

/* structure to handle user inputs */
struct user_inputs {
    char *server;
    int port;
    int thread;
    char *workload;
    char *download;
    int request;
    char *metric;
};

/* static global variable */
static steque_t qwork;
static steque_t qmetric;
static pthread_mutex_t qworklock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t qmetriclock = PTHREAD_MUTEX_INITIALIZER;
struct user_inputs inputs;
static unsigned long server_addr_nbo;

/* function to process user inputs */
static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    /* get the pointer for user_inputs */
    struct user_inputs *inputs = state->input;
    char *str_test = 0;

    switch (key) {
    case 's':
        inputs->server = arg;
        break;
    case 'p':
        inputs->port = strtol(arg, &str_test, 10);
        if (strlen(str_test) != 0 || inputs->port < 0)
            argp_error(state, "port number needs to be a non-negative integer");
        break;
    case 't':
        inputs->thread = strtol(arg, &str_test, 10);
        if (strlen(str_test) != 0 || inputs->thread > 1000 ||
            inputs->thread < 1)
            argp_error(state,
                       "number of worker thread must be in range 1-1000");
        break;
    case 'w':
        if (strlen(arg) > MAX_FILENAME_LENGTH)
            argp_error(state, "workload file name is too long");
        inputs->workload = arg;
        break;
    case 'd':
        if (strlen(arg) > MAX_FILENAME_LENGTH)
            argp_error(state, "download directory name is too long");
        if (access(arg, F_OK) != 0 || access(arg, W_OK) != 0)
            argp_error(state,
                       "download directory doesn't exist or not writable");
        struct stat fs;
        if (0 != stat(arg, &fs))
            argp_error(state,
                       "download directory doesn't exist or not writable");
        if (!(fs.st_mode & S_IFDIR))
            argp_error(state,
                       "download directory doesn't exist or not writable");
        char *real_path_1 = realpath(".", 0);
        char *real_path_2 = realpath(arg, 0);
        if (strcmp(real_path_1, real_path_2) == 0) {
            free(real_path_1);
            free(real_path_2);
            argp_error(state,
                       "download directory cannot be the current directory");
        }
        free(real_path_1);
        free(real_path_2);
        inputs->download = arg;
        break;
    case 'r':
        inputs->request = strtol(arg, &str_test, 10);
        if (strlen(str_test) != 0 || inputs->request > 1000 ||
            inputs->request < 1)
            argp_error(state, "number of requests must be in range 1-1000");
        break;
    case 'm':
        inputs->metric = arg;
        break;
    case 'h':
        argp_state_help(state, stdout, ARGP_HELP_STD_HELP);
        break;
    case ARGP_KEY_ARG:
        /* there shoud no arguments, only options */
        argp_usage(state);
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

/* structure needed by function: argp_parse */
static struct argp argp = { options, parse_opt, 0, 0 };

/* a helper function to start client socket */
//static int
int
start_client_socket(struct user_inputs inputs)
{

    int socket_fd = 0;

    // Create socket (IPv4, stream-based, protocol likely set to TCP)
    if (0 > (socket_fd = socket(AF_INET, SOCK_STREAM, 0))) {
        fprintf(stderr, "client failed to create socket\n");
        exit(1);
    }

    return socket_fd;
}

/* a helper function to make connection to server */
//static int
int
start_connect(int socket_fd, struct user_inputs inputs)
{

    // Configure server socket address structure (init to zero, IPv4,
    // network byte order for port and address) 
    struct sockaddr_in server_socket_addr;
    bzero(&server_socket_addr, sizeof(server_socket_addr));
    server_socket_addr.sin_family = AF_INET;
    server_socket_addr.sin_port = htons(inputs.port);
    server_socket_addr.sin_addr.s_addr = server_addr_nbo;

    // Connect socket to server
    if (0 >
        connect(socket_fd, (struct sockaddr *) &server_socket_addr,
                sizeof(server_socket_addr))) {
        fprintf(stderr, "client failed to connect to %s:%d!\n", inputs.server,
                inputs.port);
        close(socket_fd);
        exit(1);
    } else {
        fprintf(stdout, "client connected to to %s:%d!\n", inputs.server,
                inputs.port);
    }

    return 0;
}

/* read filesize and file content chunk by chunk */
static int
receive_file(int socket, const char *path, const char *filename, int filesize)
{

    // decide whether save the data to a file
    int download_flg = 0;

    if (strlen(filename) == 0 || strlen(path) == 0 || strcmp(path, "null") == 0)
        download_flg = 0;
    else
        download_flg = 1;
    //printf("download_flg:%i\n", download_flg);

    // get full download filename
    char fullpath[BUFFER_SIZE];
    //bzero(fullpath, BUFFER_SIZE);
    strcpy(fullpath, path);
    strcat(fullpath, "/");

    char c = filename[0];
    char *newfile = (char *) filename;
    for (int i = 0; c != '\0'; i++) {
        c = filename[i];
        if (c == '/')
            newfile = (char *) (filename + i + 1);
        //printf("%c\n", c);
    }

    strcat(fullpath, newfile);

    //printf("fullpath:%s\n", filename);

    FILE *file = NULL;
    if (access(fullpath, F_OK) == 0) {
        download_flg = 0;
    } else {
        file = fopen(fullpath, "w");
        //printf("fullpath:%s\n", fullpath);
        if (file == NULL)
            download_flg = 0;
    }
    //printf("download_flg:%i\n", download_flg);

    // read the content chunk by chunk
    fprintf(stdout, "downloading from server\n");
    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);
    int total = 0;
    int byte = 0;
    int byte_write = 0;
    int total_write = 0;
    while (1 && filesize != 0) {
        byte = recv(socket, buffer, BUFFER_SIZE, 0);
        if (byte < 0) {
            fprintf(stderr, "client could not read from socket\n");
            return 1;
        }
        total = byte + total;
        //fprintf(stdout, "received %i byte\n", total);
        // write to file
        if (download_flg) {
            byte_write = fwrite(buffer, 1, byte, file);
            total_write = total_write + byte_write;
            //fprintf(stdout, "wrote %i byte\n", total_write);
            if (byte_write <= 0)
                fprintf(stderr, "client could not write to file\n");
            if (byte != byte_write)
                fprintf(stderr, "byte read != byte write\n");
        }

        if (total == filesize) {
            //    printf("end of file\n");
            break;
        }
    }
    if (total != filesize) {
        fprintf(stderr, "received incomplete file\n");
        return 1;
    }
    fprintf(stdout, "received %i byte\n", total);
    fprintf(stdout, "done\n");
    if (download_flg) {
        if (total_write == total) {
            fprintf(stdout, "downloaded file saved\n");
        } else {
            fprintf(stdout, "saved incomplete file\n");
        }
        fclose(file);
    }

    return 0;
}

/* a function to process response from server */
//static int
int
process_response(int socket, struct user_inputs inputs, const char *filename,
                 int *total_byte)
{

    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);

    if (recv(socket, buffer, strlen(FILE_FOUND), MSG_WAITALL) <
        strlen(FILE_FOUND)) {
        fprintf(stdout,
                "cannot read response from server\n"
                "perhaps server is down ...\n");
        return 1;
    }
    // file does not exist
    if (strncmp(buffer, FILE_NOT_FOUND, strlen(FILE_FOUND)) == 0) {
        int byte =
            recv(socket, buffer, strlen(FILE_NOT_FOUND) - strlen(FILE_FOUND),
                 MSG_WAITALL);
        if (byte != strlen(FILE_NOT_FOUND) - strlen(FILE_FOUND)) {
            fprintf(stdout, "cannot read response from server\n"
                    "perhaps server is down ...\n");
            return 1;
        }
        fprintf(stdout, "%s\n", FILE_NOT_FOUND);
        return 0;
    }
    // file does exist
    fprintf(stdout, "%s\n", FILE_FOUND);
    bzero(buffer, BUFFER_SIZE);
    int i = 0;
    while (1) {
        char temp = '\0';
        if (recv(socket, &temp, 1, MSG_WAITALL) <= 0) {
            fprintf(stdout, "cannot read response from server\n"
                    "perhaps server is down ...\n");
            return 1;
        }
        if (temp == ' ')
            break;
        buffer[i] = temp;
        i++;
    }
    fprintf(stdout, "filesize:%s\n", buffer);
    *total_byte = strtol(buffer, NULL, 10);
    // receive file data
    if (0 != receive_file(socket, inputs.download, filename, *total_byte))
        return 1;
    return 0;
}

/* return current wall time */
double
get_wall_time()
{
    struct timeval time;
    if (gettimeofday(&time, NULL)) {
        return 0;
    }
    return (double) time.tv_sec + (double) time.tv_usec * .000001;
}

/* job task struct */
struct task {
    int id;
    char *filename;
};

/* job metrics struct */
struct metric {
    int id;
    int thread;
    char *filename;
    float response_time;
    float throughput;
    int total_byte;
};

/* worker main program */
void *
worker_main(void *arg)
{

    int id = *(int *) arg;

    while (1) {

        // get job from queue
        struct task *job;
        pthread_mutex_lock(&qworklock);

        if (steque_isempty(&qwork)) {
            pthread_mutex_unlock(&qworklock);
            break;
        }
        job = steque_pop(&qwork);
        pthread_mutex_unlock(&qworklock);

        if (job == NULL)
            break;

        //start start job
        fprintf(stdout, "-------------\n");
        fprintf(stdout, "thread #%i working on job #%i (%s)\n", id, job->id,
                job->filename);

        // get wall time
        double begin, end;
        double time;
        begin = get_wall_time();

        // create metric
        struct metric *result = malloc(sizeof(struct metric));
        assert(result != NULL);

        // create client socket
        int socket_fd = start_client_socket(inputs);
        if (socket_fd < 0)
            exit(1);

        // connect to server
        if (start_connect(socket_fd, inputs) != 0)
            exit(1);

        // send request
        char buffer[BUFFER_SIZE];
        bzero(buffer, BUFFER_SIZE);
        strcat(buffer, FILE_REQUEST);
        strcat(buffer, job->filename);
        int byte = send(socket_fd, buffer, strlen(buffer) + 1, MSG_NOSIGNAL);
        if (0 > byte) {
            fprintf(stderr, "client failed to send command message\n"
                    "perhaps server has disconnected ...\n");
            break;
        }
        // process response from server
        process_response(socket_fd, inputs, job->filename, &result->total_byte);

        // calculate running time
        end = get_wall_time();
        time = end - begin;

        // write metric result
        result->id = job->id;          // job id
        result->thread = id;           // thread id
        result->filename = job->filename;
        result->response_time = time;
        result->throughput = result->total_byte / time;

        // finished job
        free(job);

        // write metric result to queue
        pthread_mutex_lock(&qmetriclock);
        steque_enqueue(&qmetric, result);
        pthread_mutex_unlock(&qmetriclock);

        // close client socket
        close(socket_fd);

    }
    pthread_exit(0);
}

/* main program */
int
main(int argc, char **argv)
{

    // read default inputs
    inputs.server = "0.0.0.0";
    inputs.port = 8888;
    inputs.thread = 1;
    inputs.workload = "workload.txt";
    inputs.download = "null";
    inputs.request = 10;
    inputs.metric = "metrics.txt";

    // parse user inputs
    argp_parse(&argp, argc, argv, 0, 0, &inputs);

    // print out user inputs
    printf("webclient parameters:\n"
           "--->server: %s\n"
           "--->port: %d\n"
           "--->num of threads: %d\n"
           "--->workload file: %s\n"
           "--->download directory: %s\n"
           "--->number of reqeust: %d\n"
           "--->path to metrics file: %s\n",
           inputs.server, inputs.port, inputs.thread, inputs.workload,
           inputs.download, inputs.request, inputs.metric);

    // get server address
    struct hostent *he = gethostbyname(inputs.server);
    if (he == NULL) {
        fprintf(stderr, "could not resolve server: %s\n", inputs.server);
        exit(1);
    }
    server_addr_nbo = *(unsigned long *) (he->h_addr_list[0]);

    // read workload file
    char *filenames[BUFFER_SIZE];
    FILE *fwork = fopen(inputs.workload, "r");
    if (fwork == NULL) {
        fprintf(stderr, "could not read workload file\n");
        exit(1);
    }
    int i = 0;
    while (1) {
        char line[MAX_FILENAME_LENGTH + 1];
        bzero(line, MAX_FILENAME_LENGTH + 1);
        char *temp = fgets(line, MAX_FILENAME_LENGTH, fwork);
        if (temp == NULL) {
            break;
        }
        if (line[strlen(line) - 1] == '\n')
            line[strlen(line) - 1] = '\0';
        if (line[strlen(line) - 1] == '\r')
            line[strlen(line) - 1] = '\0';
        if (strlen(line) == 0) {
            continue;
        }
        char *filename = malloc(sizeof(char) * (strlen(line) + 1));
        assert(filename != NULL);
        strcpy(filename, line);
        filenames[i] = filename;
        i++;
    }
    int num_files = i;
    fclose(fwork);

    // create queue and jobs
    steque_init(&qwork);
    int id = 0;
    while (id < inputs.request) {
        for (int i = 0; i < num_files; i++) {
            struct task *t = malloc(sizeof(struct task));
            assert(t != NULL);
            char *f = malloc(sizeof(char) * (strlen(filenames[i]) + 1));
            bzero(f, strlen(filenames[i]) + 1);
            strcpy(f, filenames[i]);
            t->id = id;
            t->filename = f;
            steque_enqueue(&qwork, t);
            id++;
            if (id < inputs.request)
                break;
        }
    }

    steque_init(&qmetric);

    for (int i = 0; i < num_files; i++)
        free(filenames[i]);

    // create worker threads 
    pthread_t worker[inputs.thread];
    int workerID[inputs.thread];
    int real_no_threads = 0;
    for (int i = 0; i < inputs.thread; i++) {
        workerID[i] = i;
        if (0 != pthread_create(&worker[i], NULL, worker_main, workerID + i)) {
            fprintf(stderr, "could not create worker thread #%i\n",
                    workerID[i]);
            break;
        }
        real_no_threads++;
    }
    fprintf(stdout, "%d threads created\n", real_no_threads);

    // join all the worker threads
    for (i = 0; i < real_no_threads; i++) {
        if (0 != pthread_join(worker[i], NULL)) {
            fprintf(stderr, "could not join worker thread #%i\n", workerID[i]);
        }
    }

    // write metric to file
    FILE *file = fopen(inputs.metric, "w");
    if (file == NULL) {
        fprintf(stderr, "could not write metric to file: %s\n", inputs.metric);
        file = stdout;
    }
    while (!steque_isempty(&qmetric)) {
        struct metric *r = steque_pop(&qmetric);
        fprintf(file,
                "---------------\n"
                "job_id:%i\n"
                "filename:%s\n"
                "thread_id:%i\n"
                "filesize::%iB\n"
                "response_time:%5.3fs\n"
                "throughput:%5.0fB/s\n",
                r->id, r->filename, r->thread, r->total_byte, r->response_time,
                r->throughput);
        free(r->filename);
        free(r);
    }

    return 0;
}
