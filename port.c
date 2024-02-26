#include "utils.h"

/* Env vars:
 * 
 * 0) shm_id
 * 1) array_sem_synch_id
 * 2) so_porti
 * 3) so_merci
 * 4) so_size
 * 5) so_min_vita
 * 6) so_max_vita
 * 7) so_banchine
 * 8) so_fill
 * 9) ports_stats_shm_id
 * 10) products_stats_shm_id
 */
extern char **environ;

/* Variables, structs, unions and arrays of stats */
int so_porti, so_merci, so_fill, so_banchine, so_size, so_min_vita, so_max_vita;
int current_day=0, my_index;

int shm_id, sem_synch_id, ports_stats_shm_id, prod_stats_shm_id;

struct sigaction sa;
struct sembuf my_semops;

struct port_info my_infos;
struct port_info *ports_infos;

union semun my_semaphore_arg;

struct port_stats *all_ports_stats;

struct prod_stats *all_products_stats;

/* Methods */
void setup_env_vars();
void setup_local_structs_and_ipcs();
void create_products(int, int, int);
void notify_master_for_synch();
void port_local_free();
void check_expired_products();
int handle_swap();

int main(int argc, char const *argv[]) {
   int i;
   struct sembuf start;

   setup_env_vars();

   setup_local_structs_and_ipcs();

   create_products(so_size, so_min_vita, so_max_vita);

   notify_master_for_synch();

   start.sem_num = 2;
   start.sem_op = -1;
   start.sem_flg = 0;

   semop(sem_synch_id, &start, 1);

   while(handle_swap());
   
   return 0;
}

void handle_signal(int signum) {
   switch (signum) {
      case SIGUSR1:
         port_local_free();
         exit(EXIT_SUCCESS);
         break;
      case SIGUSR2:
         current_day++;
         check_expired_products();
         break;
      case SIGINT:
         port_local_free();
         exit(EXIT_SUCCESS);
         break;
      case SIGTERM:
         port_local_free();
         exit(EXIT_SUCCESS);
         break;
      default:
         break;
   }
}

void setup_env_vars() {
   shm_id = atoi(environ[0]);
   sem_synch_id = atoi(environ[1]);
   so_porti = atoi(environ[2]);
   so_merci = atoi(environ[3]);
   so_size = atoi(environ[4]);
   so_min_vita = atoi(environ[5]);
   so_max_vita = atoi(environ[6]);
   so_banchine = atoi(environ[7]);
   so_fill = atoi(environ[8]);
   ports_stats_shm_id = atoi(environ[9]);
   prod_stats_shm_id = atoi(environ[10]);
}

void setup_local_structs_and_ipcs() {
   int i, msg_id, cont = 1;

   bzero(&sa, sizeof(sa));
   sa.sa_handler = handle_signal;
   sigaction(SIGUSR1, &sa, NULL);
   sigaction(SIGUSR2, &sa, NULL);
   sigaction(SIGINT, &sa, NULL);
   sigaction(SIGTERM, &sa, NULL);

   srand(getpid());

   all_ports_stats = (struct port_stats *)shmat(ports_stats_shm_id, NULL, 0);

   all_products_stats = (struct prod_stats *)shmat(prod_stats_shm_id, NULL, 0);

   ports_infos = (struct port_info *)shmat(shm_id, NULL, 0);
   if (ports_infos == (struct port_info *)-1) {
      printf("Error during shmat in port.c\n");
      perror("shmat");
   }

   /* Setup the semaphore that represents the quays  */
   for(i=0; i<so_porti && cont; i++) {
      if(ports_infos[i].port_pid == getpid()) {
         my_index = i;
         so_banchine = 1 + (rand() % so_banchine);

         ports_infos[i].quays_id = semget(IPC_PRIVATE, 1, 0666);
         my_infos.quays_id = ports_infos[i].quays_id;
         my_semaphore_arg.val = so_banchine;

         if(semctl(ports_infos[i].quays_id, 0, SETVAL, my_semaphore_arg) == -1) {
            perror("Error setting semaphore value");
            exit(EXIT_FAILURE);
         }

         msg_id = msgget(IPC_PRIVATE, 0666);
         ports_infos[i].msg_queue_id = msg_id;
         my_infos.msg_queue_id = msg_id;

         all_ports_stats[my_index].total_quays = so_banchine;
         all_ports_stats[my_index].occupied_quays = 0;
         cont = 0;
      }
   }
}

void create_products(int so_size, int so_min_vita, int so_max_vita) {
   int i, first_offer_ind = 0, first_demand_ind = 0, tons = 0, life = 0;
   int current_fill_offer = 0, current_fill_demand = 0;

   ports_infos[my_index].my_products_offer = 
      (struct product *) shmat(ports_infos[my_index].off_shm_id, NULL, 0);
   ports_infos[my_index].my_products_demand = 
      (struct product *) shmat(ports_infos[my_index].dem_shm_id, NULL, 0);

   /*
    * 
    * In this phase we want to make sure that the port offers and demands 
    * at least one product: we draw 2 indexes included in the interval [0 ; SO_MERCI-1]
    * 
    */

   /* First offer */

   first_offer_ind = rand() % so_merci;

   ports_infos[my_index].my_products_offer[first_offer_ind].product_id = first_offer_ind;
   ports_infos[my_index].my_products_demand[first_offer_ind].product_id = first_offer_ind;
   
   tons = 1 + (rand() % so_size);
   current_fill_offer += tons;
   ports_infos[my_index].my_products_offer[first_offer_ind].ton = tons;
   all_ports_stats[my_index].tons_available += tons;
   ports_infos[my_index].my_products_demand[first_offer_ind].ton = 0;

   life = so_min_vita + (rand() % (so_max_vita-so_min_vita+1));
   ports_infos[my_index].my_products_offer[first_offer_ind].product_life = life;
   ports_infos[my_index].my_products_demand[first_offer_ind].product_life = 0;

   ports_infos[my_index].my_products_offer[first_offer_ind].status = 1;
   ports_infos[my_index].my_products_demand[first_offer_ind].status = 0;

   ports_infos[my_index].my_products_offer[first_offer_ind].product_semaphore =
      semget(IPC_PRIVATE, 1, 0666);
   my_semaphore_arg.val = ports_infos[my_index].my_products_offer[first_offer_ind].ton;
   semctl(ports_infos[my_index].my_products_offer[first_offer_ind].product_semaphore, 0, SETVAL, my_semaphore_arg);
   ports_infos[my_index].my_products_demand[first_offer_ind].product_semaphore = -1;

   /* First demand */

   do {
      first_demand_ind = rand() % so_merci;
   } while (first_demand_ind == first_offer_ind);

   ports_infos[my_index].my_products_demand[first_demand_ind].product_id = first_demand_ind;
   ports_infos[my_index].my_products_offer[first_demand_ind].product_id = first_demand_ind;
   
   tons = 1 + (rand() % so_size);
   current_fill_demand += tons;
   ports_infos[my_index].my_products_demand[first_demand_ind].ton = tons;
   ports_infos[my_index].my_products_offer[first_demand_ind].ton = 0;

   ports_infos[my_index].my_products_demand[first_demand_ind].product_life = 0;
   ports_infos[my_index].my_products_offer[first_demand_ind].product_life = 0;

   ports_infos[my_index].my_products_demand[first_demand_ind].status = 0;
   ports_infos[my_index].my_products_offer[first_demand_ind].status = 0;

   ports_infos[my_index].my_products_demand[first_demand_ind].product_semaphore =
      semget(IPC_PRIVATE, 1, 0666);
   my_semaphore_arg.val = ports_infos[my_index].my_products_demand[first_demand_ind].ton;
   semctl(ports_infos[my_index].my_products_demand[first_demand_ind].product_semaphore, 0, SETVAL, my_semaphore_arg);
   ports_infos[my_index].my_products_offer[first_demand_ind].product_semaphore = -1;

   /* 
    * 
    * In this phase we setup the two arrays without worring about the SO_FILL value: we flip a
    * coin in order to decide if the current product is going to go in the offer or demand array.
    * We are going to skip the previously valued offer and demand
    * 
    */

   for(i=0; i<so_merci; i++) {
      if(i != first_offer_ind && i != first_demand_ind) {
         ports_infos[my_index].my_products_offer[i].product_id = i;
         ports_infos[my_index].my_products_demand[i].product_id = i;
         if(rand() % 2) { /* Coin flip -> port will offer this product*/
            do {
               tons = 1 + (rand() % so_size);
            } while( (current_fill_offer + tons) > so_fill);

            current_fill_offer += tons;
            
            ports_infos[my_index].my_products_offer[i].ton = tons;
            all_ports_stats[my_index].tons_available += tons;
            ports_infos[my_index].my_products_demand[i].ton = 0;

            life = so_min_vita + (rand() % (so_max_vita-so_min_vita+1));
            ports_infos[my_index].my_products_offer[i].product_life = life;
            ports_infos[my_index].my_products_demand[i].product_life = 0;

            ports_infos[my_index].my_products_offer[i].status = 1;
            ports_infos[my_index].my_products_demand[i].status = 0;

            ports_infos[my_index].my_products_offer[i].product_semaphore =
               semget(IPC_PRIVATE, 1, 0666);
            my_semaphore_arg.val = ports_infos[my_index].my_products_offer[i].ton;
            semctl(ports_infos[my_index].my_products_offer[i].product_semaphore, 0, SETVAL, my_semaphore_arg);
            ports_infos[my_index].my_products_demand[i].product_semaphore = -1;

         } else { /* Port will demand this product */
            do {
               tons = 1 + (rand() % so_size);
            } while( (current_fill_demand + tons) > so_fill);

            current_fill_demand += tons;
            
            ports_infos[my_index].my_products_demand[i].ton = tons;
            ports_infos[my_index].my_products_offer[i].ton = 0;

            ports_infos[my_index].my_products_demand[i].product_life = 0;
            ports_infos[my_index].my_products_offer[i].product_life = 0;

            ports_infos[my_index].my_products_demand[i].status = 0;
            ports_infos[my_index].my_products_offer[i].status = 0;

            ports_infos[my_index].my_products_demand[i].product_semaphore =
               semget(IPC_PRIVATE, 1, 0666);
            my_semaphore_arg.val = ports_infos[my_index].my_products_demand[i].ton;
            semctl(ports_infos[my_index].my_products_demand[i].product_semaphore, 0, SETVAL, my_semaphore_arg);
            ports_infos[my_index].my_products_offer[i].product_semaphore = -1;
         }
      }
   }

   /*
    * 
    * In this phase we will verify if the port reaches the SO_FILL quantity
    * for both the offer and the demand: if it doesn't we will go the first valued 
    * product in the array of interest and we will increase the tons in order to
    * reach the designated quantity
    * 
    */

   if(current_fill_offer < so_fill) {
      ports_infos[my_index].my_products_offer[first_offer_ind].ton += so_fill - current_fill_offer;
      my_semaphore_arg.val = ports_infos[my_index].my_products_offer[first_offer_ind].ton;
      semctl(ports_infos[my_index].my_products_offer[first_offer_ind].product_semaphore, 0, SETVAL, my_semaphore_arg);
      all_ports_stats[my_index].tons_available += so_fill - current_fill_offer;
      current_fill_offer = so_fill;
   }
   if(current_fill_demand < so_fill) {
      ports_infos[my_index].my_products_demand[first_demand_ind].ton += so_fill - current_fill_demand;
      my_semaphore_arg.val = ports_infos[my_index].my_products_demand[first_demand_ind].ton;
      semctl(ports_infos[my_index].my_products_demand[first_demand_ind].product_semaphore, 0, SETVAL, my_semaphore_arg);
   }
}

/*
 * This method is used to notify the master and let him know that
 * my setup phase is complete and that I'm ready to start
 */
void notify_master_for_synch() {
   int sem_id = atoi(environ[1]);
   struct sembuf my_synch;

   my_synch.sem_num = 0;
   my_synch.sem_op = -1;
   my_synch.sem_flg = 0;

   semop(sem_id, &my_synch, 1);
}

void port_local_free() { 

   semctl(ports_infos[my_index].quays_id, 0, IPC_RMID);

   shmdt(ports_infos[my_index].my_products_offer);
   shmctl(ports_infos[my_index].off_shm_id, IPC_RMID, NULL);

   shmdt(ports_infos[my_index].my_products_demand);
   shmctl(ports_infos[my_index].dem_shm_id, IPC_RMID, NULL);

   shmdt(ports_infos);
}

void check_expired_products() {
   int i, val;

   ports_infos[my_index].my_products_offer = 
      (struct product *) shmat(ports_infos[my_index].off_shm_id, NULL, 0);
   
   for(i=0; i<so_merci; i++) {
      if(ports_infos[my_index].my_products_offer[i].ton > 0 && ports_infos[my_index].my_products_offer[i].status == 1) {
         if(ports_infos[my_index].my_products_offer[i].product_life <= current_day) {
            val = semctl(ports_infos[my_index].my_products_offer[i].product_semaphore, 0, GETVAL);
            all_ports_stats[my_index].tons_available -= val;
            all_ports_stats[my_index].tons_expired += val;
            all_products_stats[i].available_port -= val;
            all_products_stats[i].expired_port += val;
            my_semaphore_arg.val = 0;
            semctl(ports_infos[my_index].my_products_offer[i].product_semaphore, 0, SETVAL, my_semaphore_arg);
            ports_infos[my_index].my_products_offer[i].status = 4;
            ports_infos[my_index].my_products_offer[i].ton = 0;
         }
      }
   }
}

int handle_swap() {
   struct my_msgbuf new_req, new_ack;
   struct my_ackbuf confirmation;
   sigset_t my_mask;

   /* Waiting for a message from a ship */

   while(msgrcv(ports_infos[my_index].msg_queue_id, &new_req, sizeof(struct my_msgbuf) - sizeof(long), 1, 0) == -1);

   if(new_req.type == 0) {
      ports_infos[my_index].my_products_offer = (struct product *)shmat(ports_infos[my_index].off_shm_id, NULL, 0);

      if(ports_infos[my_index].my_products_offer[new_req.prod_id].product_life <= current_day ||
         ports_infos[my_index].my_products_offer[new_req.prod_id].ton < new_req.tons) {
         /* The request is not idoneus */
         new_ack.type = -1;
      } else {
         new_ack.type = new_req.type;
      }
   } else {
      new_ack.type = new_req.type;
   }
   
   new_ack.mtype = (long) new_req.sender;
   new_ack.sender = new_req.sender;
   new_ack.prod_id = new_req.prod_id;
   new_ack.tons = new_req.tons;

   while(msgsnd(ports_infos[my_index].msg_queue_id, &new_ack, sizeof(struct my_msgbuf) - sizeof(long), 0) == -1);

   /* 
    * Just communicated to the demanding ship that the request was not idoneus,
    * about to restart the method and wait for another message
    */
   if(new_ack.type == -1) { 
      return 1;
   }

   /*
    * The request was idoneous, now waiting for the ship communication that regards
    * the end of the nanosleep that represents the exchange of the product
    */

   while(msgrcv(ports_infos[my_index].msg_queue_id, &confirmation, sizeof(struct my_ackbuf) - sizeof(long), 100, 0) == -1);

   sigemptyset(&my_mask);
   sigaddset(&my_mask, SIGUSR2);
   sigprocmask(SIG_BLOCK, &my_mask, NULL); 

   /*
    * Updating local infos and stats
    */

   if(confirmation.type == 0) {
      all_ports_stats[my_index].tons_available -= confirmation.tons;
      all_ports_stats[my_index].tons_shipped += confirmation.tons;
      all_products_stats[confirmation.prod_id].available_port -= confirmation.tons;

      ports_infos[my_index].my_products_offer = 
         (struct product *) shmat(ports_infos[my_index].off_shm_id, NULL, 0);

      ports_infos[my_index].my_products_offer[confirmation.prod_id].ton -= confirmation.tons;

      if(confirmation.tons != new_req.tons) {
         my_semaphore_arg.val = semctl(ports_infos[my_index].my_products_offer[confirmation.prod_id].product_semaphore, 0, GETVAL);
         my_semaphore_arg.val += abs(new_req.tons - confirmation.tons);
         semctl(ports_infos[my_index].my_products_offer[confirmation.prod_id].product_semaphore, 0, SETVAL, my_semaphore_arg);
      }
   } else {
      all_ports_stats[my_index].tons_delivered += confirmation.tons;
      all_products_stats[confirmation.prod_id].delivered += confirmation.tons;

      ports_infos[my_index].my_products_demand = 
         (struct product *) shmat(ports_infos[my_index].dem_shm_id, NULL, 0);

      ports_infos[my_index].my_products_demand[confirmation.prod_id].ton =
         ports_infos[my_index].my_products_demand[confirmation.prod_id].ton - confirmation.tons;
      if(confirmation.tons != new_req.tons) {
         my_semaphore_arg.val = semctl(ports_infos[my_index].my_products_demand[confirmation.prod_id].product_semaphore, 0, GETVAL);
         my_semaphore_arg.val += abs(new_req.tons - confirmation.tons);
         semctl(ports_infos[my_index].my_products_demand[confirmation.prod_id].product_semaphore, 0, SETVAL, my_semaphore_arg);
      }
   }

   sigprocmask(SIG_UNBLOCK, &my_mask, NULL);

   /* 
    * End of communications, procedure ended successfully
    */

   confirmation.mtype = (long) confirmation.sender;

   while(msgsnd(ports_infos[my_index].msg_queue_id, &confirmation, sizeof(struct my_ackbuf) - sizeof(long), 0) == -1);

   return 1;
}
