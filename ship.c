#include "utils.h"

/* Env vars:
 * 
 * 0) shm_id
 * 1) array_sem_synch
 * 2) so_porti
 * 3) so_merci
 * 4) so_lato
 * 5) so_speed
 * 6) so_capacity
 * 7) so_loadspeed
 * 8) ship_stats_shm_id
 * 9) ports_stats_shm_id
 * 10) products_stats_shm_id
 */
extern char **environ;

/* Variables, structs and arrays*/

struct ship_info my_infos;
struct port_info *ports_infos;

/* This array represents the list of products currently loaded on the ship */
struct product *current_cargo;

/*
 * These arrays contain, respectively, the indexes of: 
 *    - the ports in shared memory
 *    - the products offered by the destination port
 *    - the products currently loaded on the ship
 * These arrays are used during the merge sort 
 */

int *sorted_ports;
int *sorted_products;
int *sorted_cargo;

int shm_id, sem_id, ship_stats_shm_id, ports_stats_shm_id, prod_stats_shm_id;
int current_status = 0; /* 0 -> Empty, 1 -> Loaded, 2 -> In port*/
int so_porti, so_capacity, so_merci;
int port_dest_index = -1, current_day=0, load_counter=0, current_capacity;
float so_speed, so_lato, so_loadspeed;

/* 
 * This array will contain the stats about the ships. The array will have 3 elements:
 * - In first (0) position there will be the counter of the empty ships
 * - In second (1) position there will be the counter of the loaded ships
 * - In third (2) position there will be the counter of the ships currently in a port
*/ 
int *all_ships_stats;

struct port_stats *all_ports_stats;

struct prod_stats *all_products_stats;

/* Methods */

void ship_config();
void ship_malloc_and_shm();
void notify_master_for_synch();
void ship_local_free();

float get_distance(float, float);
int compare_by_distance(int, int);
void ports_merge(int, int, int);
void ports_merge_sort(int, int);

int compare_by_expirance(int, int, int);
void products_merge(int, int, int, int);
void products_merge_sort(int, int, int);

void access_leave_port(int);
int navigate();
int *demanding_ports(int);
int reserve_product(int, int);
int load_unload_product(int, int, int);

void check_expiring_products();

void my_sleep(time_t, long);

int main(int argc, char const *argv[]) {
   struct sigaction sa;
   struct sembuf start;
   int i, j;

   bzero(&sa, sizeof(sa));
   sa.sa_handler = handle_signal;
   sigaction(SIGUSR1, &sa, NULL);
   sigaction(SIGUSR2, &sa, NULL);

   srand(getpid());

   ship_config();

   ship_malloc_and_shm();

   notify_master_for_synch();

   start.sem_num = 2;
   start.sem_op = -1;
   start.sem_flg = 0;

   semop(sem_id, &start, 1);

   while(navigate());
   
   ship_local_free();
   return 0;
}

void handle_signal(int signum) {
   int i;
   switch (signum) {
      case SIGUSR1:
         ship_local_free();
         exit(EXIT_SUCCESS);
         break;
      case SIGUSR2:
         current_day++;
         check_expiring_products();
         break;
      case SIGINT:
         ship_local_free();
         exit(EXIT_SUCCESS);
         break;
      case SIGTERM:
         ship_local_free();
         exit(EXIT_SUCCESS);
         break;
      default:
         break;
   }
}

/*
 * This method initializes the configuration variables and the coordinates of the ship
 */
void ship_config() {
   shm_id = atoi(environ[0]);
   sem_id = atoi(environ[1]);
   so_porti = atoi(environ[2]);
   so_merci = atoi(environ[3]);
   so_lato = atof(environ[4]);
   so_speed = atof(environ[5]);
   so_capacity = atoi(environ[6]);
   current_capacity = so_capacity;
   so_loadspeed = atof(environ[7]);
   ship_stats_shm_id = atoi(environ[8]);
   ports_stats_shm_id = atoi(environ[9]);
   prod_stats_shm_id = atoi(environ[10]);

   my_infos.coord_x = (float)rand() / RAND_MAX * so_lato;
   my_infos.coord_y = (float)rand() / RAND_MAX * so_lato;
}

void ship_malloc_and_shm() {
   int i;

   ports_infos = (struct port_info *)shmat(shm_id, NULL, 0);
   if(ports_infos == (struct port_info *)-1) {
      printf("Error during shmat in ship.c\n");
      perror("shmat");
   }

   for(i=0; i<so_porti; i++) {
      ports_infos[i].my_products_offer = (struct product *) shmat(ports_infos[i].off_shm_id, NULL, 0);
      ports_infos[i].my_products_demand = (struct product *) shmat(ports_infos[i].dem_shm_id, NULL, 0);
   }

   current_cargo = malloc(so_merci * sizeof(struct product));
   sorted_products = malloc(so_merci * sizeof(int));
   sorted_cargo = malloc(so_merci * sizeof(int));
   for(i=0; i<so_merci; i++) {
      current_cargo[i].product_id = i;
      sorted_products[i] = i;
      sorted_cargo[i] = i;
   }
   

   sorted_ports = malloc(so_porti * sizeof(int));
   for(i=0; i<so_porti; i++) {
      sorted_ports[i] = i;
   }

   all_ships_stats = (int *)shmat(ship_stats_shm_id, NULL, 0);
   all_ships_stats[0]++;
   current_status = 0;

   all_ports_stats = (struct port_stats *)shmat(ports_stats_shm_id, NULL, 0);
   all_products_stats = (struct prod_stats *)shmat(prod_stats_shm_id, NULL, 0);
}

/*
 * This method is used to notify the master and let him know that
 * my setup phase is complete and that I'm ready to start
 */
void notify_master_for_synch() {
   struct sembuf my_synch;

   my_synch.sem_num = 1;
   my_synch.sem_op = -1;
   my_synch.sem_flg = 0;

   semop(sem_id, &my_synch, 1);
}

void ship_local_free() {
   free(current_cargo);
   free(sorted_ports);
   free(sorted_products);
   free(sorted_cargo);
}

/*
 * This method is used to determine which port between
 * 'a' and 'b' is closer to the ship, where 'a' and 'b'
 * are the indexes of the ports
 */
int compare_by_distance(int a, int b) {
   float distance_a = 0.0, distance_b = 0.0;

   ports_infos = (struct port_info *)shmat(shm_id, NULL, 0);

   distance_a = get_distance(ports_infos[a].coord_x, ports_infos[a].coord_y);
   distance_b = get_distance(ports_infos[b].coord_x, ports_infos[b].coord_y);

   if(distance_a < distance_b) {
      return -1;
   } else if(distance_a > distance_b) {
      return 1;
   } else {
      return 0;
   }
}

float get_distance(float coord_x, float coord_y) {
   float x_diff, y_diff;

   x_diff = coord_x - my_infos.coord_x;
   y_diff = coord_y - my_infos.coord_y;

   return sqrt((x_diff) * (x_diff) + (y_diff) * (y_diff));
} 

/*
 * Implementation of the merge sort used to sort the ports
 * from closest to farthest to the ship 
 */
void ports_merge(int left, int mid, int right) {
   int n1 = mid - left + 1, n2 = right - mid, i, j, k;
   int *leftArray = (int *)malloc(n1 * sizeof(int));
   int *rightArray = (int *)malloc(n2 * sizeof(int));

   for (i = 0; i < n1; i++) {
      leftArray[i] = sorted_ports[left + i];
   }
   for (i = 0; i < n2; i++) {
      rightArray[i] = sorted_ports[mid + 1 + i];
   }
      
   i = 0;
   j = 0;
   k = left;

   while (i < n1 && j < n2) {
      if (compare_by_distance(leftArray[i], rightArray[j]) <= 0) {
         sorted_ports[k] = leftArray[i];
         i++;
      } else {
         sorted_ports[k] = rightArray[j];
         j++;
      }
      k++;
   }

   while (i < n1) {
      sorted_ports[k] = leftArray[i];
      i++;
      k++;
   }

   while (j < n2) {
      sorted_ports[k] = rightArray[j];
      j++;
      k++;
   }

   free(leftArray);
   free(rightArray);
}

/*
 * Implementation of the merge sort used to sort the ports
 * from closest to farthest to the ship 
 */
void ports_merge_sort(int left, int right) {
   int mid;

   if(left < right) {
      mid = left + (right - left) / 2;
      ports_merge_sort(left, mid); 
      ports_merge_sort(mid+1, right);
      ports_merge(left, mid, right);
   }
}

/*
 * This method is used to determine which product between 'a' and 'b' expires sooner, 
 * therefore the method determines which product is the most urgent. 
 * The parameters 'a' and 'b' are the indexes of the products.
 * The parameter port_id can assume different values:
 *    - if it equals -1 then it means that the products that the method has to
 *      compare are loaded on the ship
 *    - otherwise the index must be used to access the port infos in shared memory
 *      and examine the port offer
 */
int compare_by_expirance(int port_id, int prod_a, int prod_b) {
   if(port_id != -1) {
      ports_infos[port_id].my_products_offer = (struct product *)shmat(ports_infos[port_id].off_shm_id, NULL, 0);
      if(ports_infos[port_id].my_products_offer[prod_a].product_life < ports_infos[port_id].my_products_offer[prod_b].product_life) {
         return -1;
      } else if(ports_infos[port_id].my_products_offer[prod_a].product_life > ports_infos[port_id].my_products_offer[prod_b].product_life) {
         return 1;
      } else {
         return 0;
      }
   } else {
      if(current_cargo[prod_a].product_life < current_cargo[prod_b].product_life) {
         return -1;
      } else if(current_cargo[prod_a].product_life > current_cargo[prod_b].product_life) {
         return 1;
      } else {
         return 0;
      }
   }
}

/*
 * Implementation of the merge sort used to sort the products from most to less urgent.
 * The parameter port_id can assume different values:
 *    - if it equals -1 then it means that the method has to sort the product 
 *      loaded on the ship
 *    - otherwise the index must be used to access the port infos in shared memory
 *      and sort the port offer
 */
void products_merge(int port_id, int left, int mid, int right) {
   int n1 = mid - left + 1, n2 = right - mid, i, j, k;
   int *leftArray = (int *)malloc(n1 * sizeof(int));
   int *rightArray = (int *)malloc(n2 * sizeof(int));

   if(port_id != -1) {
      for (i=0; i<n1; i++) {
         leftArray[i] = sorted_products[left + i];
      }
   } else {
      for (i=0; i<n1; i++) {
         leftArray[i] = sorted_cargo[left + i];
      }
   }
   
   if(port_id != -1) {
      for (i = 0; i < n2; i++) {
         rightArray[i] = sorted_products[mid + 1 + i];
      }
   } else {
      for (i = 0; i < n2; i++) {
         rightArray[i] = sorted_cargo[mid + 1 + i];
      }
   }
      
   i = 0;
   j = 0;
   k = left;

   while (i < n1 && j < n2) {
      if (compare_by_expirance(port_id, leftArray[i], rightArray[j]) <= 0) {
         if(port_id != -1) {
            sorted_products[k] = leftArray[i];
         } else {
            sorted_cargo[k] = leftArray[i];
         }
         i++;
      } else {
         if(port_id != -1) {
            sorted_products[k] = rightArray[j];
         } else {
            sorted_cargo[k] = rightArray[j];
         }
         j++;
      }
      k++;
   }

   while (i < n1) {
      if(port_id != -1) {
         sorted_products[k] = leftArray[i];
      } else {
         sorted_cargo[k] = leftArray[i];
      }
      i++;
      k++;
   }

   while (j < n2) {
      if(port_id != -1) {
         sorted_products[k] = rightArray[j];
      } else {
         sorted_cargo[k] = rightArray[j];
      }
      j++;
      k++;
   }

   free(leftArray);
   free(rightArray);
}

/*
 * Implementation of the merge sort used to sort the products from most to less urgent.
 * The parameter port_id can assume different values:
 *    - if it equals -1 then it means that the method has to sort the product 
 *      loaded on the ship
 *    - otherwise the index must be used to access the port infos in shared memory
 *      and sort the port offer
 */
void products_merge_sort(int port_id, int left, int right) {
   int mid;

   if(left < right) {
      mid = left + (right - left) / 2;
      products_merge_sort(port_id, left, mid); 
      products_merge_sort(port_id, mid+1, right);
      products_merge(port_id, left, mid, right);
   }
}

/* 
 * This method is used to access or leave a port, operating on the "quays" semaphore.
 * The "action" parameter determines the behaviour of the method:
 *    - if "action" equals "-1", the ship is trying to access the port
 *    - if "action" equals "1", the ship is trying to leave the port
 */
void access_leave_port(int action) {
   struct sembuf my_op;
   int result;  

   my_op.sem_num = 0;
   my_op.sem_flg = 0;
   my_op.sem_op = action;

   do {
      result = semop(ports_infos[port_dest_index].quays_id, &my_op, 1);
   } while(errno == EINTR && result == -1);
}

/*
 * This method is used by the ship to take charge of the transportation of a product.
 * If, during the evaluation phase, the trip to the port is evaluated as doable, the
 * ship must take charge of the transportation of a product in a certain quantity
 * expressed in tons (the return value of the method). In order to determine this quantity,
 * the method inspects the value of the semaphore of the given product, since the value in 
 * shared memory might not be updated.
 * The "mode" parameter determines the behaviour of the method:
 *    - if "mode" equals "0", then the ship intends to load a product on board
 *    - if "mode" equals "1", then the ship intends to deliver the product to the port
 */
int reserve_product(int prod_ind, int mode) {
   struct sembuf my_reserve;
   int result, max_quantity = -1, curr_sem_val = 0;

   if(mode == 0) {
      ports_infos[port_dest_index].my_products_offer = 
      (struct product *) shmat(ports_infos[port_dest_index].off_shm_id, NULL, 0);

      curr_sem_val = semctl(ports_infos[port_dest_index].my_products_offer[prod_ind].product_semaphore, 0, GETVAL);

      if(curr_sem_val <= 0) {
         return -1;
      }

      if(curr_sem_val < current_capacity) {
         max_quantity = curr_sem_val;
      } else {
         max_quantity = current_capacity;
      }

      my_reserve.sem_num = 0;
      my_reserve.sem_op = -(max_quantity);
      my_reserve.sem_flg = 0;

      do {
         result = semop(ports_infos[port_dest_index].my_products_offer[prod_ind].product_semaphore, &my_reserve, 1);
      } while(errno == EINTR && result == -1);
   } else {
      ports_infos[port_dest_index].my_products_demand = 
         (struct product *) shmat(ports_infos[port_dest_index].dem_shm_id, NULL, 0);

      curr_sem_val = semctl(ports_infos[port_dest_index].my_products_demand[prod_ind].product_semaphore, 0, GETVAL);
      
      if(curr_sem_val <= 0) {
         return -1;
      }

      if(curr_sem_val < current_cargo[prod_ind].ton) {
         max_quantity = curr_sem_val;
      } else {
         max_quantity = current_cargo[prod_ind].ton;
      }

      my_reserve.sem_num = 0;
      my_reserve.sem_op = -(max_quantity);
      my_reserve.sem_flg = 0;

      do {
         result = semop(ports_infos[port_dest_index].my_products_demand[prod_ind].product_semaphore, &my_reserve, 1);
      } while(errno == EINTR && result == -1);

   }

   return max_quantity;
}

/*
 * This method is used to exchange products with a port.
 * The "mode" parameter determines the behaviour of the method:
 *    - if "mode" equals "0", then the ship intends to load a product on board
 *    - if "mode" equals "1", then the ship intends to deliver the product to the port
 */
int load_unload_product(int prod_ind, int quantity, int mode) {
   time_t seconds;
   struct my_msgbuf new_msg, new_reply;
   struct my_ackbuf confirmation;
   int i;
   sigset_t my_mask;
   
   if(mode == 0) {
      ports_infos[port_dest_index].my_products_offer = 
         (struct product *) shmat(ports_infos[port_dest_index].off_shm_id, NULL, 0);
      new_msg.type = 0;
      new_msg.prod_id = ports_infos[port_dest_index].my_products_offer[prod_ind].product_id;
   } else {
      ports_infos[port_dest_index].my_products_demand = 
         (struct product *) shmat(ports_infos[port_dest_index].dem_shm_id, NULL, 0);
      new_msg.type = 1;
      new_msg.prod_id = ports_infos[port_dest_index].my_products_demand[prod_ind].product_id;
   }

   new_msg.mtype = (long) 1;
   new_msg.sender = getpid();
   new_msg.tons = quantity;

   /* Sending a message to the destination port to notify him of my presence on a quay */

   while(msgsnd(ports_infos[port_dest_index].msg_queue_id,&new_msg,sizeof(struct my_msgbuf)-sizeof(long), 0)==-1);
   
   while(msgrcv(ports_infos[port_dest_index].msg_queue_id,&new_reply,sizeof(struct my_msgbuf)-sizeof(long),(long) getpid(),0)==-1);

   /* 
    * Once I receive a reply I either know if the port is available and ready to start the exchange
    * or if my request was not suitable; in this case the method will end here
    */

   if(new_reply.type == -1) {
      return -1;
   }

   /*
    * Before making the trip to the port I estimated how much time I needed to arrive to the destination
    * and load/unload the prefixed product in order to evaluate the suitability of the trip. A thing that
    * I didn't consider was the time needed to have my request accepted by the port: if there are many 
    * quays occupied I might have to wait for some time. In this case, it might happen that, 
    * while I'm loading/unloading the product, the lot might expire, generating inconsistencies and making
    * my trip to the port pointless. In the following lines, to avoid this case, if needed I recalibrate the 
    * quantity of product that I will load/unload, in order to maximize my trips and in order to try to move
    * successfully as much tons of products as possible.
    */

   seconds = (time_t)(quantity / so_loadspeed);
   if(mode == 0) {
      ports_infos[port_dest_index].my_products_offer = 
         (struct product *) shmat(ports_infos[port_dest_index].off_shm_id, NULL, 0);
      if(ports_infos[port_dest_index].my_products_offer[prod_ind].product_life <= seconds + current_day) {
         do {
            quantity = quantity / 2;
            seconds = (time_t)(quantity / so_loadspeed);
         } while(ports_infos[port_dest_index].my_products_offer[prod_ind].product_life <= seconds + current_day);

      }
   } else {
      if(current_cargo[prod_ind].product_life <= seconds + current_day) {
         do {
            quantity = quantity / 2;
            seconds = (time_t)(quantity / so_loadspeed);
         } while(current_cargo[prod_ind].product_life <= seconds + current_day);
      }
   }

   my_sleep(seconds, (long) (((quantity / so_loadspeed) - seconds) * 1e9));
   confirmation.mtype = (long) 100;
   if(mode == 0) {
      confirmation.type = 0;
   } else {
      confirmation.type = 1;  
   }
   confirmation.prod_id = new_reply.prod_id;
   confirmation.sender = new_reply.sender;
   confirmation.tons = quantity;

   /* 
    * Nanosleep just ended, notifying the port and waiting for him 
    * to update its local infos and global stats 
    */

   while(msgsnd(ports_infos[port_dest_index].msg_queue_id, &confirmation, sizeof(struct my_ackbuf) - sizeof(long), 0) == -1);
      
   while(msgrcv(ports_infos[port_dest_index].msg_queue_id, &confirmation, sizeof(struct my_ackbuf) - sizeof(long), (long) getpid(), 0) == -1);

   sigemptyset(&my_mask);
   sigaddset(&my_mask, SIGUSR2);
   sigprocmask(SIG_BLOCK, &my_mask, NULL); 

   all_products_stats = (struct prod_stats *)shmat(prod_stats_shm_id, NULL, 0);

   /* Updating local infos and stats */

   if(mode == 0) {
      all_products_stats[prod_ind].on_ship += quantity;

      ports_infos[port_dest_index].my_products_offer = 
         (struct product *) shmat(ports_infos[port_dest_index].off_shm_id, NULL, 0);

      current_cargo[prod_ind].product_id = ports_infos[port_dest_index].my_products_offer[prod_ind].product_id;
      current_cargo[prod_ind].ton = quantity;
      current_capacity -= current_cargo[prod_ind].ton;
      current_cargo[prod_ind].product_life = ports_infos[port_dest_index].my_products_offer[prod_ind].product_life;
      current_cargo[prod_ind].status = 2;
      load_counter++;
   } else {
      all_products_stats[prod_ind].on_ship -= quantity;
      current_cargo[prod_ind].ton -= quantity;
      current_capacity += quantity;
      if(current_cargo[prod_ind].ton == 0) {
         current_cargo[prod_ind].status = 0;
         current_cargo[prod_ind].product_life = 0;
         load_counter--;
      }
   }

   sigprocmask(SIG_UNBLOCK, &my_mask, NULL); 

   return 1;
}

/*
 * This method handles the bigger part of the lifecycle of a ship:
 *    - determines the best trip to make
 *    - determines which product must be loaded/unloaded first
 *    - simulates the navigation
 *    - handles the access to a port and the leaving as well
 *    - handles the loading/unloading procedures 
 *    - finally updates some stats
 */
int navigate() {
   int most_urgent_index = -1, tons_quantity = 0, i=0, j=0, k=0, cont = 1, check = 0, estimated_tons = 0;
   int action; /* 0 load, 1 unload */
   float distance;
   int *my_ports;
   time_t seconds, estimated_sec;

   if(current_capacity == so_capacity) { /* Ship is empty */
      ports_merge_sort(0, so_porti-1);
      ports_infos = (struct port_info *)shmat(shm_id, NULL, 0);
      for(i=0; i<so_porti && cont; i++) {
         products_merge_sort(sorted_ports[i], 0, so_merci-1);
         ports_infos[sorted_ports[i]].my_products_offer = (struct product *)shmat(ports_infos[sorted_ports[i]].off_shm_id, NULL, 0);
         for(j=0; j<so_merci && cont; j++) {
            if(ports_infos[sorted_ports[i]].my_products_offer[sorted_products[j]].ton > 0) {
               /* Estimating how many tons I can load and how much time it will take to do so, including the navigation */
               estimated_tons = ports_infos[sorted_ports[i]].my_products_offer[sorted_products[j]].ton;
               distance = get_distance(ports_infos[sorted_ports[i]].coord_x, ports_infos[sorted_ports[i]].coord_y);
               seconds = (time_t) (distance / so_speed);
               estimated_sec = seconds;
               if(estimated_tons > so_capacity) {
                  estimated_sec += (time_t) (so_capacity / so_loadspeed);
               } else {
                  estimated_sec += (time_t) (estimated_tons / so_loadspeed);
               }
               if(ports_infos[sorted_ports[i]].my_products_offer[sorted_products[j]].product_life > estimated_sec + current_day) {
                  port_dest_index = sorted_ports[i];
                  most_urgent_index = sorted_products[j];
                  if((tons_quantity = reserve_product(most_urgent_index, 0)) > 0) {
                     action = 0;
                     cont = 0;
                  }
               }
            }
         }
      }
      if(cont == 1) {
         return 1;
      }
   } else { /* Ship is loaded */
      products_merge_sort(-1, 0, so_merci-1);
      ports_merge_sort(0, so_porti-1);
      for(i=0; i<so_merci && cont; i++) {
         if(current_cargo[sorted_cargo[i]].ton > 0) {
            most_urgent_index = sorted_cargo[i];
            my_ports = demanding_ports(current_cargo[most_urgent_index].product_id);
            if(my_ports != NULL) {
               for(j=0; j<so_porti && cont; j++) { /* Iterating on demanding ports ordered by distance */
                  if(my_ports[sorted_ports[j]] == 1) { 
                     /* Estimating how many tons I can unload and how much time it will take to do so, including the navigation */
                     port_dest_index = sorted_ports[j];
                     ports_infos = (struct port_info *)shmat(shm_id, NULL, 0);
                     ports_infos[port_dest_index].my_products_demand = shmat(ports_infos[port_dest_index].dem_shm_id, NULL, 0);
                     estimated_tons = ports_infos[port_dest_index].my_products_demand[most_urgent_index].ton;
                     distance = get_distance(ports_infos[port_dest_index].coord_x, ports_infos[port_dest_index].coord_y);
                     seconds = (time_t) (distance / so_speed);
                     estimated_sec = seconds;
                     if(estimated_tons > current_cargo[most_urgent_index].ton) {
                        estimated_sec += (time_t) (current_cargo[most_urgent_index].ton / so_loadspeed);
                     } else {
                        estimated_sec += (time_t) (estimated_tons / so_loadspeed);
                     }
                     if(current_cargo[most_urgent_index].product_life > estimated_sec + current_day) {
                        if((tons_quantity = reserve_product(most_urgent_index, 1)) > 0) {
                           action = 1;
                           cont = 0;
                        }
                     }
                  }
               }
               if(cont == 1) { /* All the demanding ports are unreachable */
                  return 1;
               }
            } else { /* Nobody is demanding this product */
               return 1;
            }
         }
      }
      if(cont == 1) {
         return 1;
      }
   }

   /* Navigating to the port and updating my coordinates */

   my_sleep(seconds, (long) (((distance / so_speed) - seconds) * 1e9));
   my_infos.coord_x = ports_infos[port_dest_index].coord_x;
   my_infos.coord_y = ports_infos[port_dest_index].coord_y;

   access_leave_port(-1);

   all_ports_stats[port_dest_index].occupied_quays++;
   
   action == 1 ? all_ships_stats[1]-- : all_ships_stats[0]--;
   all_ships_stats[2]++;
   current_status = 2;

   if(action == 1) { /* Ship has to unload something */
      load_unload_product(most_urgent_index, tons_quantity, 1);
      cont = 1;
      while(current_capacity != so_capacity && cont == 1) { /* Still loaded, let's see if the ship can unload something else */
         products_merge_sort(-1, 0, so_merci-1);
         ports_infos[port_dest_index].my_products_demand = shmat(ports_infos[port_dest_index].dem_shm_id, NULL, 0);
         for(i=0; i<so_merci && cont; i++) {
            if(current_cargo[sorted_cargo[i]].ton > 0) {
               if(ports_infos[port_dest_index].my_products_demand[current_cargo[sorted_cargo[i]].product_id].ton > 0) {
                  if(current_cargo[sorted_cargo[i]].product_life > current_day) {
                     most_urgent_index = current_cargo[sorted_cargo[i]].product_id;
                     if((tons_quantity = reserve_product(most_urgent_index, 1)) > 0) {
                        load_unload_product(most_urgent_index, tons_quantity, 1);
                     }
                  }
               }
            }
         }
         if(cont == 0 && current_capacity != so_merci) { /* Unloaded something but maybe there's more to unload, retry */
            cont = 1;
         } else if(i == so_merci) { /* Cannot unload anything else */
            cont = 0;
         }
      }
      /* At this point the ship unloaded everything suitable product,
       * now let's check if something can be loaded before leaving */
      products_merge_sort(port_dest_index, 0, so_merci-1);
      ports_infos[port_dest_index].my_products_offer = shmat(ports_infos[port_dest_index].off_shm_id, NULL, 0);
      cont = 1;
      for(i=0; i<so_merci && cont; i++) {
         if(ports_infos[port_dest_index].my_products_offer[sorted_products[i]].ton > 0) {
            if(ports_infos[port_dest_index].my_products_offer[sorted_products[i]].product_life > current_day) {
               most_urgent_index = sorted_products[i];
               if((tons_quantity = reserve_product(most_urgent_index, 0)) > 0) {
                  action = 0;
                  cont = 0;
               }
            }
         }
      }
   }

   if(action == 0) { /* Ship is going to load something */
      load_unload_product(most_urgent_index, tons_quantity, 0);
      cont = 1;
      while(current_capacity > 0 && cont == 1) { /* Some space left, let's see if the ship can load something else */
         products_merge_sort(port_dest_index, 0, so_merci-1);
         for(i=0; i<so_merci && cont; i++) {
            ports_infos[port_dest_index].my_products_offer = shmat(ports_infos[port_dest_index].off_shm_id, NULL, 0);
            if(ports_infos[port_dest_index].my_products_offer[sorted_products[i]].ton > 0) {
               if(ports_infos[port_dest_index].my_products_offer[sorted_products[i]].product_life > current_day) {
                  most_urgent_index = sorted_products[i];
                  if((tons_quantity = reserve_product(most_urgent_index, 0)) > 0) {
                     load_unload_product(most_urgent_index, tons_quantity, 0);
                  }
               }
            }
         }
         if(i == so_merci) {
            cont = 0;
         }
      }
   }

   /* Loading / Unloading procedure completed, now leaving the port and updating some stats */

   access_leave_port(1);
   all_ports_stats[port_dest_index].occupied_quays--;

   all_ships_stats[2]--;
   if(current_capacity == so_capacity) { 
      all_ships_stats[0]++;
      current_status = 0;
   } else { 
      all_ships_stats[1]++;
      current_status = 1;
   }

   return 1;
}

/*
 * This method returns an array of integers where the elements initialized with "1" indicating
 * that the correspondent port is demanding the given product.
 */
int *demanding_ports(int prod_id) {
   int *res = malloc(so_porti * sizeof(int));
   int i, at_least_one = 0, check = 0;

   ports_infos = (struct port_info *)shmat(shm_id, NULL, 0);

   for(i=0; i<so_porti; i++) {
      ports_infos[sorted_ports[i]].my_products_demand = (struct product *)shmat(ports_infos[sorted_ports[i]].dem_shm_id, NULL, 0);
      check = ports_infos[sorted_ports[i]].my_products_demand[prod_id].ton != 0;
      if(check) {
         res[sorted_ports[i]] = 1;
         at_least_one++;
      } else {
         res[sorted_ports[i]] = 0;
      }
   }

   return (at_least_one > 0) ? res :  NULL;
}

void check_expiring_products() {
   int i;
   
   for(i=0; i<so_merci; i++) {
      if(current_cargo[i].ton > 0 && current_cargo[i].product_life <= current_day) {
         all_products_stats[i].on_ship -= current_cargo[i].ton;
         all_products_stats[i].expired_ship += current_cargo[i].ton;
         current_capacity += current_cargo[i].ton;
         current_cargo[i].ton = 0;
         current_cargo[i].status = 0;
         current_cargo[i].product_life = 0;
         load_counter--;
         if(load_counter == 0 && current_status == 1) {
            all_ships_stats[1]--;
            all_ships_stats[0]++;
            current_status = 0;
         }
      }
   }
}

/*
 * This method executes a nanosleep with the given parameters.
 * It uses a while in order to keep working properly when, during the nanosleep,
 * a signal is delivered to the process and the signal handler is executed
 */
void my_sleep(time_t seconds, long nano) {
   struct timespec sleeping, remaining;

   sleeping.tv_sec = seconds;
   sleeping.tv_nsec = nano;
   while(nanosleep(&sleeping, &remaining) == -1) {
      sleeping.tv_sec = remaining.tv_sec;
      sleeping.tv_nsec = remaining.tv_nsec;
   }
}
