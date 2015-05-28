/* ===========================================================================
 * Introduction to Opertating Systems
 * CS 8803, CT 0MSCS
 *
 * Project 1: Multi-Threaded Webserver
 * Name: Fujia Wu
 * GTID: 902869936
 *
 * webserver.c
 * Implement the webserver
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
#include <assert.h>
#include <sys/time.h>
#include <pthread.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <regex.h>

#include "steque.h"
#include "shm_channel.h"

#define BUFFER_SIZE 4096
#define FILE_NOT_FOUND "GetFile FILE_NOT_FOUND 0 "
#define FILE_FOUND "GetFile OK "
#define FILE_REQUEST "GetFile GET "
#define MAX_FILENAME_LENGTH 200

/* use GNU arg_parse library to parse arguments & options */

/* bug address for help page */
const char *argp_program_bug_address = "<fujiawu@gatech.edu>";

/* option specification */
static struct argp_option options[] = {
    {0, 'p', "port", 0, "port (Default: 8888)"},
    {0, 't', "thread", 0,
     "number of worker threads (Default: 1, Range: 1-1000)"},
    {0, 'f', "file", 0, "path to static files (Default: .)"},
    {0, 's', "server", 0, "server name (Default: Udacity S3)"},
    {0, 'n', "segments", 0, "number of shm segments (Default: 4096)"},
    {0, 'z', "segment_size", 0, "size of shm segment size (Default: 10)"},
    {0, 'h', 0, 0, "show help message"},
    {0}
};

/* struct to handle user inputs */
struct user_inputs {
    int port;
    int thread;
    char *file;
    char *server;
    int nsegments;
    int segment_size;
};

/* task global variable */
static steque_t qwork;
static pthread_mutex_t qworklock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gempty = PTHREAD_COND_INITIALIZER;

struct user_inputs inputs;

/* ipc global variables */
static int msq;
static steque_t shmq;
static pthread_mutex_t shmqlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t shmqempty = PTHREAD_COND_INITIALIZER;

/* function to process user inputs */
static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    // get the pointer for user_inputs
    struct user_inputs *inputs = state->input;
    char *str_test = 0;

    switch (key) {
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
    case 'f':
        inputs->file = arg;
        if (strlen(inputs->file) > MAX_FILENAME_LENGTH)
            argp_error(state, "file directory name is too long");
        break;
    case 's':
        inputs->server = arg;
        if (strlen(inputs->file) > MAX_FILENAME_LENGTH)
            argp_error(state, "server name is too long");
        break;
    case 'n':
        inputs->nsegments = strtol(arg, &str_test, 10);
        if (strlen(str_test) != 0 || inputs->port < 0)
            argp_error(state,
                       "number of segments needs to be a non-negative integer");
        break;
    case 'z':
        inputs->segment_size = strtol(arg, &str_test, 10);
        if (strlen(str_test) != 0 || inputs->port < 0)
            argp_error(state,
                       "number of segments needs to be a non-negative integer");
        break;
    case 'h':
        argp_state_help(state, stdout, ARGP_HELP_STD_HELP);
        break;
    case ARGP_KEY_ARG:
        argp_usage(state);
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

/* structure needed by argument parser function: argp_parse */
static struct argp argp = { options, parse_opt, 0, 0 };

/* a helper function to start the server socket */
static int
start_server(struct user_inputs inputs)
{
    // print out the user inputs
    printf("webserver parameters:\n"
           "--->the path to static files: %s\n"
           "--->num of server worker threads: %i\n"
           "number of segments: %i\n"
           "segment size: %i\n"
           "web server started ...\n",
           inputs.file, inputs.thread, inputs.nsegments, inputs.segment_size);

    int socket_fd = 0;
    int set_reuse_addr = 1; // ON == 1
    int max_pending_connections = SOMAXCONN;
    struct sockaddr_in server;

    // Create socket (IPv4, stream-based, protocol likely set to TCP)
    if (0 > (socket_fd = socket(AF_INET, SOCK_STREAM, 0))) {
        fprintf(stderr, "server failed to create the listening socket\n");
        exit(1);
    }
    // Allows other sockets to bind to this port, unless there is an active listening 
    // socket bound to the port already.
    if (0 !=
        setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &set_reuse_addr,
                   sizeof(set_reuse_addr))) {
        fprintf(stderr,
                "server failed to set SO_REUSEADDR socket option (not fatal)\n");
    }
    // Configure server socket address structure (init to zero, IPv4,
    // network byte order for port and address)
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(inputs.port);

    // Bind the socket
    if (0 > bind(socket_fd, (struct sockaddr *) &server, sizeof(server))) {
        fprintf(stderr, "server failed to bind\n");
        close(socket_fd);
        exit(1);
    }
    // Listen on the socket for up to some maximum pending connections
    if (0 > listen(socket_fd, max_pending_connections)) {
        fprintf(stderr, "server failed to listen\n");
        close(socket_fd);
        exit(1);
    } else {
        fprintf(stdout, "listening on port %d ...\n", inputs.port);
        fprintf(stdout, "---------------\n");
    }
    return socket_fd;
}

/* a helper function to start to accept */
static int
accept_client(int server_socket_fd)
{

    int client_socket_fd = 0;
    struct sockaddr_in client;
    socklen_t client_addr_len;
    client_addr_len = sizeof(client);

    // Accept a new client
    if (0 >
        (client_socket_fd =
         accept(server_socket_fd, (struct sockaddr *) &client,
                &client_addr_len))) {
        fprintf(stderr, "server accept failed\n");
        return -1;
    } else {
        fprintf(stdout, "server accepted a client!\n");
    }

    return client_socket_fd;
}

/* help functio to send message to client */
static int
send_message(int socket, const char *st, size_t length)
{
    int byte = send(socket, st, length, MSG_NOSIGNAL);
    if (0 >= byte) {
        fprintf(stderr,
                "server could not write back to socket\n"
                "perhps client has disconnected ...");
        return 1;
    } else {
        fprintf(stdout, "server sent %i byte message to client\n", byte);
    }

    return 0;
}

/* send filesize and file content
 *           chunk by chunk to client 
static int
send_file(int socket, const char *filename, int filesize)
{

    // open file
    fprintf(stdout, "sending file to client\n");
    FILE *fd = fopen(filename, "r");
    if (fd == NULL) {
        fprintf(stderr, "server open file error\n");
        return 1;
    }
    // send data chunk by chunk
    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);
    int total = 0;
    int numRead = 0;
    while (1) {
        numRead = fread(buffer, 1, BUFFER_SIZE, fd);
        if (numRead < 0) {
            fprintf(stderr, "server read file error\n");
            return 1;
        }
        if (numRead == 0)
            break;
        if (numRead < BUFFER_SIZE) {
            if (feof(fd))              // EOF
                break;
            if (ferror(fd)) {
                fprintf(stderr, "server read file error\n");
                return 1;
            }
        }
        int byte = send(socket, buffer, numRead, MSG_NOSIGNAL);
        if (0 > byte) {
            fprintf(stderr,
                    "server could not write back to client\n"
                    "perhaps client has disconnected ...\n");
            return 1;
        }
        total = total + byte;
        //fprintf (stdout, "server sent %i byte data to client\n", total);
    }
    // send the last chunk of data
    int byte = send(socket, buffer, numRead, MSG_NOSIGNAL);
    if (0 > byte) {
        fprintf(stderr,
                "server could not write back to client\n"
                "perhaps client has disconnected ...\n");
        return 1;
    }
    total = total + byte;
    if (total != 0)
        fprintf(stdout, "server sent %i byte data to client\n", total);

    if (total != filesize) {
        fprintf(stderr, "sent incomplete file\n");
        return 1;
    }
    fprintf(stdout, "done\n");
    fclose(fd);
    return 0;
}
*/

/* read file name from client */
static int
read_filename(int socket, char *filename, size_t n)
{
    bzero(filename, n);
    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);

    int byte = recv(socket, buffer, strlen(FILE_REQUEST), MSG_WAITALL);
    if (byte <= 0) {
        fprintf(stdout, "server cannot read from client\n"
                "perhaps client has disconnected ...\n");
        return 2;
    }
    if (byte < strlen(FILE_REQUEST)) {
        fprintf(stdout, "server does not understand client request:\n");
        fprintf(stderr, "buffer:%s\n", buffer);
        return 1;
    }
    // check GetFile command
    if (strncmp(buffer, FILE_REQUEST, strlen(FILE_REQUEST)) != 0) {
        fprintf(stdout, "server does not understand client request\n");
        fprintf(stderr, "buffer:%s\n", buffer);
        return 1;
    }
    // read filename, ignoring whitespace
    bzero(buffer, BUFFER_SIZE);
    byte = recv(socket, buffer, MAX_FILENAME_LENGTH, 0);
    if (byte <= 0) {
        fprintf(stdout, "server does not understand client request\n");
        fprintf(stderr, "buffer:%s\n", buffer);
        return 1;
    }
    fprintf(stdout, "filename:%s\n", buffer);
    int name_len = 0;
    for (int i = 0; buffer[i] != '\0'; i++) {
        if (isspace(buffer[i]) == 0) {
            filename[strlen(filename)] = buffer[i];
            name_len++;
        }
    }

    if (name_len == 0) {
        fprintf(stdout, "server thinks the filename is empty\n");
        return 1;
    }

    return 0;
}

/* help function to send file size */
static int
send_filesize(int socket, int filesize)
{

    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);
    sprintf(buffer, "%i", filesize);
    buffer[strlen(buffer)] = ' ';
    fprintf(stdout, "filesize:%i\n", filesize);
    if (0 != send_message(socket, buffer, strlen(buffer)))
        return 1;
    return 0;
}

/* a function to read and process request from client 
static int
handle_with_file(int socket, const char *filename, struct user_inputs inputs)
{

    // get file path
    char fullpath[BUFFER_SIZE];
    strcpy(fullpath, inputs.file);
    strcat(fullpath, "/");
    strcat(fullpath, filename);

    // existence and permission to read
    if (access(fullpath, F_OK) != 0 || access(fullpath, R_OK) != 0) {
        fprintf(stdout, "this file does not exists on server\n");
        send_message(socket, FILE_NOT_FOUND, strlen(FILE_NOT_FOUND));
        return 1;
    }

    struct stat fs;
    if (0 != stat(fullpath, &fs)) {
        fprintf(stdout, "this file does not exists on server\n");
        send_message(socket, FILE_NOT_FOUND, strlen(FILE_NOT_FOUND));
        return 1;
    }

    if (!(fs.st_mode & S_IFREG)) {
        fprintf(stdout, "this file is not a regular file\n");
        send_message(socket, FILE_NOT_FOUND, strlen(FILE_NOT_FOUND));
        return 1;
    }

    fprintf(stdout, "this file exists on server\n");
    if (0 != send_message(socket, FILE_FOUND, strlen(FILE_FOUND)))
        return 2;
    // send the file size
    fprintf(stdout, "sending filesize to client\n");
    if (0 != send_filesize(socket, (int) fs.st_size))
        return 2;

    // send file data
    if (0 != send_file(socket, fullpath, (int) fs.st_size))
        return 2;
    return 0;
}
*/

/* signal handler */
static void
_sig_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {

        /* stop message queue */
        msgctl(msq, IPC_RMID, NULL);

        /* stop shared mm */
        while (!steque_isempty(&shmq)) {
            int *shmid = steque_pop(&shmq);
            shmctl(*shmid, IPC_RMID, NULL);
            free(shmid);
        }

        exit(signo);
    }
}

/* url struct */
struct _urlfcontext_t {
    int file_len;
    int copied;
    char *data;
};

typedef struct _urlfcontext_t urlfcontext_t;

/* url header processor */
static size_t
process_header(char *ptr, size_t size, size_t nmemb, void *stream)
{

    char buffer[size * nmemb + 1];
    strncpy(buffer, ptr, size * nmemb);
    buffer[size * nmemb] = '\0';

    urlfcontext_t *ufc = stream;

    int reti;
    regex_t regex;
    regmatch_t pmatch[1];
    regcomp(&regex, "Content-Length: ", 0);
    reti = regexec(&regex, buffer, 1, pmatch, 0);
    if (reti == 0 && ufc->file_len < 0) {
        ufc->file_len =
            strtol((char *) (buffer + (int) pmatch[0].rm_eo), NULL, 10);
        ufc->data = malloc(ufc->file_len);
    }
    return size * nmemb;
}

/* url data processor */
static size_t
process_data(void *ptr, size_t size, size_t nmemb, void *stream)
{

    urlfcontext_t *ufc = stream;

    if (ufc->file_len >= 0) {
        memcpy(ufc->data + ufc->copied, ptr, size * nmemb);
        ufc->copied += size * nmemb;
    }

    return size * nmemb;
}

/* handle with url */
int
handle_with_curl(int socket, const char *filename, struct user_inputs inputs)
{

    fprintf(stdout, "-- handle by curl\n");

    char fullurl[BUFFER_SIZE];
    strcpy(fullurl, inputs.server);
    //strcat(fullurl, "/");
    strcat(fullurl, filename);
    //printf("%s", fullurl);

    CURL *curl = curl_easy_init();
    if (curl == NULL)
        return 1;

    curl_easy_setopt(curl, CURLOPT_URL, fullurl);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, process_data);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, process_header);

    urlfcontext_t ufc = { -1, 0, NULL };
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ufc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ufc);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));

    curl_easy_cleanup(curl);

    if (ufc.file_len < 0)
        return 1;

    send_message(socket, FILE_FOUND, strlen(FILE_FOUND));
    send_filesize(socket, ufc.file_len);
    send(socket, ufc.data, ufc.file_len, MSG_NOSIGNAL);

    free(ufc.data);

    return 0;
}

/* handle_with_cache function */
ssize_t
handle_with_cache(int socket, const char *filename, struct user_inputs inputs)
{

    fprintf(stdout, "-- handle by cache\n");
    int file_len = 0, transferred = 0;
    struct request_msg msgbuf;

    pthread_mutexattr_t psharedm;
    pthread_condattr_t psharedc;
    pthread_mutexattr_init(&psharedm);
    pthread_mutexattr_setpshared(&psharedm, PTHREAD_PROCESS_SHARED);
    pthread_condattr_init(&psharedc);
    pthread_condattr_setpshared(&psharedc, PTHREAD_PROCESS_SHARED);

    pthread_mutex_lock(&shmqlock);
    while (steque_isempty(&shmq)) {
        pthread_cond_wait(&shmqempty, &shmqlock);
    }
    int *shmid = steque_pop(&shmq);
    pthread_mutex_unlock(&shmqlock);

    struct shmid_ds shminfo;
    shmctl(*shmid, IPC_STAT, &shminfo);
    size_t segment_size = shminfo.shm_segsz;
    if (segment_size < sizeof(struct shm_data_struct)) {
        fprintf(stderr, "segment size too small\n");
        return 1;
    }

    struct shm_data_struct *shm_data = (struct shm_data_struct *)
        shmat(*shmid, (void *) 0, 0);
    if ((char *) shm_data == (char *) (-1)) {
        fprintf(stderr, "could not map shared memory\n");
        return 1;
    }
    pthread_mutex_init(&shm_data->mutex, &psharedm);
    pthread_cond_init(&shm_data->server, &psharedc);
    pthread_cond_init(&shm_data->cache, &psharedc);
    shm_data->size = -2;
    shm_data->byte = 0;

    msgbuf.shmid = *shmid;
    msgbuf.mtype = 1;
    strcpy(msgbuf.mdata, filename);
    if (msgsnd(msq, &msgbuf, sizeof(msgbuf) - sizeof(long), 0) == -1) {
        fprintf(stderr, "could not send message\n");
        if (shmdt(shm_data) == -1)
            fprintf(stderr, "could not detach shm\n");
        return 1;
    }

    pthread_mutex_lock(&shm_data->mutex);
    while (shm_data->size == -2)
        pthread_cond_wait(&shm_data->server, &shm_data->mutex);
    file_len = shm_data->size;
    pthread_mutex_unlock(&shm_data->mutex);

    if (shm_data->size == -1) {
        return 1;
    } else {
        send_message(socket, FILE_FOUND, strlen(FILE_FOUND));
        send_filesize(socket, file_len);

        transferred = 0;
        size_t write_len = 0;
        char *buffer = (char *) shm_data + sizeof(struct shm_data_struct);
        if (shm_data->size > 0) {
            while (transferred < file_len) {
                pthread_mutex_lock(&shm_data->mutex);
                while (shm_data->byte == 0)
                    pthread_cond_wait(&shm_data->server, &shm_data->mutex);
                write_len = send(socket, buffer, shm_data->byte, MSG_NOSIGNAL);
                if (write_len != shm_data->byte) {
                    fprintf(stderr, "error sending data to client\n");
                }
                transferred += write_len;
                shm_data->byte = 0;
                pthread_cond_signal(&shm_data->cache);
                pthread_mutex_unlock(&shm_data->mutex);
            }
        }

    }

    if (shmdt(shm_data) == -1)
        fprintf(stderr, "could not detach shared memory\n");

    pthread_mutex_lock(&shmqlock);
    steque_enqueue(&shmq, shmid);
    pthread_cond_signal(&shmqempty);
    pthread_mutex_unlock(&shmqlock);

    return 0;
}

/* worker main program */
void *
worker_main(void *arg)
{

    // workers never stop
    while (1) {

        // get a job from queue
        pthread_mutex_lock(&qworklock);
        while (steque_isempty(&qwork)) {
            pthread_cond_wait(&gempty, &qworklock);
        }
        int *client = steque_pop(&qwork);
        pthread_mutex_unlock(&qworklock);

        // read filename from server
        char filename[BUFFER_SIZE];
        int status = read_filename(*client, filename, BUFFER_SIZE);
        if (status == 2) {
            close(*client);
            free(client);
            continue;
        }
        if (status == 1) {
            send_message(*client, FILE_NOT_FOUND, strlen(FILE_NOT_FOUND));
            close(*client);
            free(client);
            continue;
        }
        // handle file
        //handle_with_file(*client, filename, inputs);
        status = handle_with_cache(*client, filename, inputs);
        if (status != 0) {
            fprintf(stdout, "cache did not find it\n");
            status = handle_with_curl(*client, filename, inputs);
        }
        if (status != 0) {
            fprintf(stdout, "curl also did not find it\n");
            send_message(*client, FILE_NOT_FOUND, strlen(FILE_NOT_FOUND));
        }

        fprintf(stdout, "------------\n");

        close(*client);
        free(client);

    }
    pthread_exit(0);
}

/* main program */
int
main(int argc, char **argv)
{

    /* default values */
    inputs.port = 8888;
    inputs.thread = 1;
    inputs.file = ".";
    inputs.server = "s3.amazonaws.com/content.udacity-data.com";
    inputs.nsegments = 10;
    inputs.segment_size = 4096;

    /* parse options with GNU argp_prase function */
    argp_parse(&argp, argc, argv, 0, 0, &inputs);

    /* install signal handler */
    if (signal(SIGINT, _sig_handler) == SIG_ERR) {
        fprintf(stderr, "Can't catch SIGINT...exiting.\n");
        exit(EXIT_FAILURE);
    }

    if (signal(SIGTERM, _sig_handler) == SIG_ERR) {
        fprintf(stderr, "Can't catch SIGTERM...exiting.\n");
        exit(EXIT_FAILURE);
    }

    /* initialize message queue */
    key_t key = ftok("webserver", 'z');
    msq = msgget(key, 0644 | IPC_CREAT);
    if (msq == -1) {
        fprintf(stderr, "could not create message queue\n");
        exit(1);
    }

    /* initialize shared mem segments */
    if (inputs.segment_size < sizeof(struct shm_data_struct)) {
        fprintf(stderr, "segment size too small\n");
        exit(1);
    }
    steque_init(&shmq);
    for (int i = 0; i < inputs.nsegments; i++) {
        key_t key = ftok("webserver", (char) i);
        int *shmid = malloc(sizeof(int));
        *shmid = shmget(key, inputs.segment_size, 0644 | IPC_CREAT);
        if (*shmid == 1)
            fprintf(stderr, "could not create shared memory\n");
        steque_enqueue(&shmq, shmid);
    }

    /* set curl */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* start the server socket */
    int server = 0;
    server = start_server(inputs);
    if (server < 0)
        exit(1);

    // create worker threads
    pthread_t worker[inputs.thread];
    int workerID[inputs.thread];
    int i = 0;
    for (i = 0; i < inputs.thread; i++) {
        workerID[i] = i;
        if (0 != pthread_create(&worker[i], NULL, worker_main, workerID + i)) {
            fprintf(stderr, "could not create worker thread %i\n", workerID[i]);
            break;
        }
    }
    int real_no_threads = i;
    fprintf(stdout, "%d threads created:\n" "-------------\n", real_no_threads);

    // initialize queue
    steque_init(&qwork);

    // server does not stop by itself
    int num_request = 0;
    while (1) {

        // accept a client
        int *client = malloc(2 * sizeof(int));
        *client = accept_client(server);
        if (*client < 0) {
            close(*client);
            free(client);
            exit(1);
        }
        num_request++;

        // enter critical
        pthread_mutex_lock(&qworklock);

        // add new job to queue
        steque_enqueue(&qwork, client);
        fprintf(stdout, "Received a new client: #%d\n", num_request);

        // leave critical
        if (steque_isempty(&qwork) == 0) {
            pthread_cond_broadcast(&gempty);
        }
        pthread_mutex_unlock(&qworklock);

    }

    // close server socket
    close(server);

    // join all the worker threads
    for (int i = 0; i < real_no_threads; i++) {
        if (0 != pthread_join(worker[i], NULL)) {
            fprintf(stderr, "could not join worker thread #%i\n", workerID[i]);
        }
    }

    return 0;
}
