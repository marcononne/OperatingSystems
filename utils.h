#define _GNU_SOURCE

#define SHIP_PARAMS_COUNT 12
#define PORT_PARAMS_COUNT 12

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <float.h>
#include <math.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>

/* Structs */

/*
 * 
 * This struct represents a single product with all its relevant infos:
 *    - the ID
 *    - the tons, value in range [1 - SO_SIZE]
 *    - the product's life, which will be updated every second (day of the simulation). Value
 *      in range [SO_MIN_VITA - SO_MAX_VITA]
 *    - the product's status, which gives us info about where a product is (in a port or in a ship) or
 *      if the product is expired and eventually where (ship or port). Here are the possible values:
 *          1) Available in a port
 *          2) On a ship
 *          3) Delivered to a port
 *          4) Expired in a port
 *          5) Expired in a ship 
 *    - the id of the product' semaphore. This semaphore is used by ships in order to take charge of a single
 *      product in the context of loading it or unloading it. This semaphore is initially valorized with the
 *      ton value. Example: 
 *          The "X" port demands 100 tons of "prod_1"; the ship "1" transports 50 tons of
 *          "prod_1" so decides to consume the semaphore with a semop, leaving the sem value to 50, 
 *          committing to serve (partially) the port's demand. If the ship "2" is transporting
 *          70 tons of "prod_1" and sees that the port "X" needs 100 tons of "prod_1", the boat 
 *          may think that all of its 70 tons can be delivered to the port, but looking at the
 *          semaphore value the boat learns that only 50 tons can be delivered.
 *      With this implementation we avoid "pointless" trips due to a possible inconsistency of the 
 *      demand tons value, that needs to stay the same until the prods are actually delivered to the port
 *      for statistics purposes. This principle is applied for both the offer and the demand.
 * 
 */
struct product {
   int product_id; 
   int ton; 
   int product_life;
   int status; 
   int product_semaphore;
};

/*
 * 
 * This struct contains the infos about a single port:
 *    - his pid
 *    - his coordinates
 *    - the id of his semaphores (representing the quays of the port)
 *    - the id of his message queue
 *    - the array of products that the port offers and the correspondent shared memory id
 *    - the array of products that the port demands and the correspondent shared memory id
 */
struct port_info {
   pid_t port_pid;
   float coord_x;
   float coord_y;
   int quays_id;
   int msg_queue_id;
   int off_shm_id;
   struct product *my_products_offer;
   int dem_shm_id;
   struct product *my_products_demand;
};

/* 
 * 
 * This struct contains a single ship coordinates
 * 
 */
struct ship_info {
   float coord_x;
   float coord_y;
};

/* 
 *
 * This struct contains the configuration variables of the simulation.
 * This struct is initialized by the master in his setup phase 
 * by reading from a file specified by the user
 * 
 */
struct config_variables {
   int SO_NAVI;
   int SO_PORTI;
   int SO_MERCI;
   int SO_SIZE; 
   int SO_MIN_VITA;
   int SO_MAX_VITA;
   float SO_LATO;
   float SO_SPEED;
   int SO_CAPACITY; 
   int SO_BANCHINE;
   int SO_FILL;
   float SO_LOADSPEED;
   int SO_DAYS;
};

/*
 * 
 * This struct represents a message used by both the ports and the ships.
 * This struct is used in the first part of the exchange:
 *    - the ship is the first one to send this kind of message in order to
 *      let the port know that the ship got access to a quay and is ready 
 *      to start an exchange of products
 *    - if the request by the ship is idoneus, the port uses this type of
 *      message to reply and to let the ship know that the exchange can start
 * 
 * The mtype field can be valued in two ways:
 *    - 1 if the message is sent by the ship to the port, so that the port can read
 *      any kind of request from a ship
 *    - the pid of the demanding ship if the port is replying to a request,
 *      so that the ship can read only the relevant messages
 * 
 * The type field can be valued in two ways:
 *    - 0 if the ship intends to load a product
 *    - 1 if the ship intends to unload a product
 * 
 * The sender field is valued with the pid of the ship that starts the conversation.
 * The prod_id field is valued with the id of the product that the ship wants to load/unload.
 * The tons field is valued with the tons of product that the ship intends to load/unload
 * 
 */
struct my_msgbuf {
   long mtype; 
   int type;
   pid_t sender;
   int prod_id;
   int tons;
};

/*
 * 
 * This struct represents a message used by both the ports and the ships.
 * This struct is used in the second part of the exchange:
 *    - the ship is the first one to send this kind of message in order to
 *      let the port know that the nanosleep that simulates the time necessary
 *      to load/unload a product just ended, so that the port can update its
 *      internal infos, the infos in shared memory and the stats
 *    - at the end of the update of the above infos, the port sends to the ship
 *      this message to let the ship know that the entire procedure ended as
 *      expected, so that the ship can start to update its local infos and the stats
 * 
 * The mtype field is valued can be valued in two ways:
 *    - 100 if the ship completed the nanosleep and wants to notify the port
 *    - the pid of the demanding port
 * 
 * The other fields are valued the same way as in the my_msgbuf struct
 * 
 */
struct my_ackbuf {
   long mtype; 
   int type; 
   pid_t sender;
   int prod_id;
   int tons;
};

/* 
 * 
 * This struct contains the stats of a single product:
 *    - available_port is the counter of the tons of the product that 
 *      are available in the ports
 *    - on_ship is the counter of the tons of the product that 
 *      are currently loaded on ships
 *    - delivered is the counter of the tons of the product that 
 *      have been delivered to the ports
 *    - expired_port is the counter of the tons of the product that 
 *      expired in the ports
 *    - expired_ship is the counter of the tons of the product that 
 *      expired on the ships
 *    - top_offering_port is the pid of the port that offered the 
 *      higher quantity of tons of the product
 *    - top_demanding_port is the pid of the port that demanded the
 *      higher quantity of tons of the product
 *   
 */
struct prod_stats {
   int available_port;
   int on_ship;
   int delivered;
   int expired_port;
   int expired_ship;
   pid_t top_offering_port;
   pid_t top_demanding_port;
};

/* 
 * 
 * This struct contains the stats of a single port:
 *    - tons_available is the counter of the tons available in the port
 *    - tons_shipped is the counter of the tons shipped by the port
 *    - tons_delivered is the counter of the tons delivered to the port
 *    - tons_expired is the counter of the tons expired in the port
 *    - total_quays is the counter of the quays of the port
 *    - occupied_quays is the counter of the occupied quays in the port
 *      in any given moment
 *   
 */
struct port_stats {
   int tons_available;
   int tons_shipped;
   int tons_delivered;
   int tons_expired;
   int total_quays;
   int occupied_quays;
};

/* Union */

/*
 *
 * This struct is used to initialize the semaphores of the products for 
 * both the demand and the offer.
 * 
 */
union semun {
	int val;                   /* Value for SETVAL */
	struct semid_ds *buf;      /* Buffer for IPC_STAT and other stuff */
	unsigned short  *array;    /* Array used for GETALL, SETALL and other stuff */
	struct seminfo  *__buf;    /* Buffer for IPC_INFO */
};

/* Methods */

struct config_variables setup_config_variables(char* configuration_file);

void handle_signal(int);


