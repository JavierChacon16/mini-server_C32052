//
// Created by Sleyter Angulo on 9/17/26.
//

#include "../includes/net_util.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <semaphore.h>

#define _POSIX_C_SOURCE 200809L

#define DEFAULT_PORT 8080
#define LISTEN_BACKLOG 64
#define DRAIN_SECONDS 1


#define BUFFER_SIZE 64 //tamaño buffer
#define WORKER_THREADS 4 //cantidad de threads


static volatile sig_atomic_t g_running = 1;

static unsigned long g_requests_served = 0;



typedef struct {
    int file_descriptor;
    unsigned long connection_id;
} connection_t;


////////////BUFFER///////////////////
static connection_t *buffer[BUFFER_SIZE];



static int buffer_in = 0;
static int buffer_out = 0;


// SEMÁFOROS
 
 // mutex: Protege el acceso al buffer.solo un thread puede modificar el buffer al mismo tiempo.
 
 // items: Indica cuántos elementos hay disponibles para que los consumidores trabajen.

 //spaces:indica cuántos espacios libres quedan en el buffer. Valor inicial = BUFFER_SIZE


static sem_t mutex;
static sem_t items;
static sem_t spaces;


// mutex para evitar que se modifique requests al mismo tiempo
static sem_t requests_mutex;


static void on_sigint(int signum)
{
    (void)signum;

    g_running = 0;
}


static int install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;

    if (sigaction(SIGINT, &sa, NULL) < 0)
    {
        perror("sigaction");
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;

    if (sigaction(SIGPIPE, &sa, NULL) < 0)
    {
        perror("sigaction");
        return -1;
    }

    return 0;
}


// WORKER / CONSUMER
 
 /*
    1.Esperar a que exista un item.
    2. Entrar al buffer protegido por mutex.
    3. Sacar una conexión.
    4. Liberar el mutex.
    5. Liberar un espacio del buffer.
    6. Procesar la conexión.
    
    items.wait()
    mutex.wait()
    event = buffer.get()
    mutex.signal()
    spaces.signal()
    event.process()
    */

static void *worker(void *arg)
{
    (void)arg;


    while (g_running)
    {
        connection_t *conn;


      // 1.Esperar a que exista un item.    
        if (sem_wait(&items) != 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("sem_wait(items)");
            break;
        }


        
    //Si el servidor recibió SIGINT Break
        
        if (!g_running)
        {
            break;
        }


        ///2. Entrar al buffer protegido por mutex.      
        sem_wait(&mutex);


          // 3. Sacar una conexión.

        conn = buffer[buffer_out];

        buffer_out = (buffer_out + 1) % BUFFER_SIZE;


               //    4. Liberar el mutex.

        sem_post(&mutex);


    // 5. Liberar un espacio del buffer.

        sem_post(&spaces);


       //  6. Procesar la conexión.

        printf("[Worker] Processing connection %lu\n",
               conn->connection_id);

        fflush(stdout);

        if (nu_drain_request(conn->file_descriptor) < 0)
        {
            (void)nu_send_response( conn->file_descriptor, conn->connection_id );
        }


        
        sem_wait(&requests_mutex);

        unsigned long current = g_requests_served;

        sched_yield();

        g_requests_served = current + 1;

        sem_post(&requests_mutex);


        
        if (close(conn->file_descriptor) < 0)
        {
            perror("close(file_descriptor)");
        }

        free(conn);
    }


    return NULL;
}


//PRODUCER
 
  /*El productor hace:

    1. Esperar por un espacio disponible.
    2. Obtener el mutex.
    3. Agregar la conexión al buffer.
    4. Liberar el mutex.
    5. Avisar que existe un nuevo item.


    spaces.wait()
    mutex.wait()
    buffer.add(event)
    mutex.signal()
    items.signal()


 */
static void produce_connection(connection_t *conn)
{
       //  1. Esperar por un espacio disponible.
        sem_wait(&spaces);
    
    //2. Obtener el mutex.
    sem_wait(&mutex);


   // 3. Agregar la conexión al buffer.
    buffer[buffer_in] = conn;
    buffer_in = (buffer_in + 1) % BUFFER_SIZE;

   //4. Liberar el mutex.
    sem_post(&mutex);


    // 5. Avisar que existe un nuevo item.
    sem_post(&items);
}

static unsigned short parse_port(int argc, char **argv)
{
    if (argc < 2)
    {
        return DEFAULT_PORT;
    }

    char *end = NULL;
    errno = 0;

    long value= strtol(argv[1], &end, 10);

    if (errno != 0 || end == argv[1] || *end != '\0' ||
        value <= 0 || value > 65535)    {
        fprintf(stderr,"invalid port '%s', using %d\n", argv[1],DEFAULT_PORT);
        return DEFAULT_PORT;
    }

    return (unsigned short)value;
}

int main(int argc, char **argv)
{
   
    if (install_signal_handlers() < 0)
    {
        return EXIT_FAILURE;
    }
    
    unsigned short port = parse_port(argc, argv);
  
    int listen_file_descriptor = nu_listen(port, LISTEN_BACKLOG);

    if (listen_file_descriptor < 0)
    {
        return EXIT_FAILURE;
    }


    printf("listening on port %u — Ctrl-C to stop\n", port);
    fflush(stdout);


       //INICIALIZAR SEMAFOROS
    sem_init(&mutex, 0, 1);
    sem_init(&items, 0, 0);
    sem_init(&spaces, 0, BUFFER_SIZE);

    sem_init(&requests_mutex, 0, 1);

      //CREAR WORKER
    pthread_t worker_threads[WORKER_THREADS];

    for (int i = 0; i < WORKER_THREADS; ++i){
        int pthread_created =pthread_create(&worker_threads[i],NULL, worker,NULL);
        if (pthread_created != 0)
        {
         fprintf(stderr,"pthread_create failed %s\n",strerror(pthread_created));
        }
        else
        {
           pthread_created =pthread_detach(worker_threads[i]);
            if (pthread_created != 0){
             fprintf(stderr,"pthread_detach failed %s\n",strerror(pthread_created));
            }
        }
    }
    unsigned long accepted = 0;


     //PRODUCTOR MAIN MODIFICADO
    while (g_running)
    {
            int client_file_descriptor =accept(listen_file_descriptor,NULL,NULL);
        if (client_file_descriptor < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("accept");
            break;
        }
        connection_t *conn = malloc(sizeof(connection_t));

        if (conn == NULL)
        {
            fprintf(stderr, "out of memory, dropping connection\n");
            close(client_file_descriptor);
            continue;        }

        conn->file_descriptor = client_file_descriptor;
        conn->connection_id = ++accepted;

  //PRUDCIR
        produce_connection(conn);
    }
 
    if (close(listen_file_descriptor)){
        perror("close(listen_file_descriptor)");
    }
    sleep(DRAIN_SECONDS);
    sem_wait(&requests_mutex);

    unsigned long served = g_requests_served;

    sem_post(&requests_mutex);

    printf("\naccepted: %lu\n", accepted);
    printf("served:   %lu\n", served);
    printf("lost:     %ld\n", (long)accepted -(long)served);
  
    sem_destroy(&mutex);
    sem_destroy(&items);
    sem_destroy(&spaces);
    sem_destroy(&requests_mutex);

    return EXIT_SUCCESS;
}