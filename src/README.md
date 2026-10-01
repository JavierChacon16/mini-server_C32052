## Descripción

Modificación del Mini-Server en C para implementar el patrón
Productor-Consumidor utilizando hilos POSIX y semáforos.

El hilo principal funciona como productor y coloca las conexiones
recibidas en un búfer compartido. Los Workers funcionan como
consumidores y procesan las conexiones.

## Requisitos

- Linux / Ubuntu
- GCC
- POSIX Threads
- Semáforos POSIX

## Compilación

Desde la carpeta `src`:

```bash
gcc -Wall -Wextra -pthread server_safe.c net_util.c -o server_safe
gcc -Wall -Wextra -pthread load_client.c net_util.c -o load_client
```

## Ejecución

Primero iniciar el servidor:

```bash
./server_safe
```

El servidor utiliza por defecto el puerto `8080`.

Luego, desde otra terminal, ejecutar el cliente:

```bash
./load_client 127.0.0.1 8080 1 150
```


## Cantidad de Workers

La cantidad de Workers se modifica en `server_safe.c`:

```c
#define WORKER_THREADS 4
```

## Pruebas con diferentes Cores

Para utilizar un núcleo:

```bash
taskset -c 0 ./server_safe
```

Para utilizar dos núcleos:

```bash
taskset -c 0,1 ./server_safe
```

## Medición del tiempo

Para medir el tiempo del cliente:

```bash
/usr/bin/time -f "Tiempo: %e segundos" ./load_client 127.0.0.1 8080 1 150
```


## Resultados

| Prueba | Cores | Workers | Paquetes | Tiempo (s) | Paquetes/s |
|---|---:|---:|---:|---:|---:|
| 1 | 1 | 1 | 150 | 0.04 | 3750.00 |
| 2 | 1 | 2 | 150 | 0.08 | 1875.00 |
| 3 | 1 | 4 | 150 | 0.06 | 2500.00 |
| 4 | 1 | 8 | 150 | 0.06 | 2500.00 |
| 5 | 2 | 1 | 150 | 0.15 | 1000.00 |
| 6 | 2 | 2 | 300 | 0.07 | 4285.71 |
| 7 | 2 | 4 | 300 | 0.12 | 2500.00 |


## Estructura

```text
mini-server_C32052/
├── src/
│   ├── server_safe.c
│   ├── server_unsafe.c
│   ├── load_client.c
│   └── net_util.c
└── README.md

```

## Repositorio

https://github.com/JavierChacon16/mini-server_C32052.git
```