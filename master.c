#include "utils.h"

/* Variables and structs */
pid_t* ports_pids;
pid_t* ships_pids;

/*
 * This arrays are used to pass params thorugh the execve to the ships and the ports processes
 */
char **port_params, **ship_params;

int shm_id, ship_stats_shm_id, ports_stats_shm_id, prod_stats_shm_id;
int sem_synch_id;
int current_day = 0, ended = 0;

struct config_variables my_config_variables;

/*
 * This array will be in shared memory and will contain the infos
 * about the ports involved in the simulation
 */
struct port_info *ports_infos;

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

void choose_config();
void master_malloc_and_ipcs();
void signal_to_everyone(int);
void free_existing_data_structures();
void find_best_ports();
void print_stats();
void check_global_offer();

int main(void) {
   pid_t pid_port, pid_ship;
   int i, j, ris;
   char *args[] = {NULL};
   struct sigaction sa;
   struct timespec my_timeout;
   struct sembuf ports_and_ships_sync;

   /* Signals and Nanosleep setup */
   bzero(&sa, sizeof(sa));
   sa.sa_handler = handle_signal;
   sigaction(SIGALRM, &sa, NULL);
   sigaction(SIGINT, &sa, NULL);
   sigaction(SIGTERM, &sa, NULL);

   my_timeout.tv_sec = 1;

   /* Config choice, Malloc for arrays of pids, array of structs and shared memory */
   choose_config();
   
   master_malloc_and_ipcs();

   /* Ports and ships creation */

   for(i=0; i<my_config_variables.SO_PORTI; i++) {
      pid_port = fork();
      
      srand(pid_port);
      
      if(pid_port == -1) {
         perror("fork failed!");
         exit(EXIT_FAILURE);
      } else if(pid_port == 0) {
         execve("./port", args, port_params);
         perror("execve ports error");
         exit(EXIT_FAILURE);
      } else {
         ports_pids[i] = pid_port;
         if(i < 4) { /* Disposing 4 ports in the corners of the map */
            if(i==0) {
               ports_infos[i].coord_x = 0.00;
               ports_infos[i].coord_y = 0.00;
            } else {
               if(i==1) {
                  ports_infos[i].coord_x = 0.00;
                  ports_infos[i].coord_y = my_config_variables.SO_LATO;
               } else {
                  if(i==2) {
                     ports_infos[i].coord_x = my_config_variables.SO_LATO;
                     ports_infos[i].coord_y = 0.00;
                  } else {
                     ports_infos[i].coord_x = my_config_variables.SO_LATO;
                     ports_infos[i].coord_y = my_config_variables.SO_LATO;
                  }
               }
            }
         } else {
            ports_infos[i].coord_x = (float)rand() / RAND_MAX * my_config_variables.SO_LATO;
            ports_infos[i].coord_y = (float)rand() / RAND_MAX * my_config_variables.SO_LATO;
         }
         ports_infos[i].port_pid = pid_port;
      }
   }

   /*
    * Waiting for all the ports to complete their setup.
    * This semaphore was previously initialized to SO_PORTI and each time a
    * port ends its setup procedure, the semaphore is consumed: when it
    * reaches a value equal to 0 the master knows that every port ended its setup
    */

   ports_and_ships_sync.sem_num = 0;
   ports_and_ships_sync.sem_op = 0;
   ports_and_ships_sync.sem_flg = 0;

   semop(sem_synch_id, &ports_and_ships_sync, 1);

   find_best_ports();

   for(i=0; i<my_config_variables.SO_NAVI; i++) {
      pid_ship = fork();
      if(pid_ship == -1) { 
         perror("fork failed!");
         exit(EXIT_FAILURE);
      } else if(pid_ship == 0) { 
         execve("./ship", args, ship_params);
         perror("execve ships error");
         exit(EXIT_FAILURE);
      } else {
         ships_pids[i] = pid_ship;
      }
   }

   /*
    * As described above for the ports but now for the ships.
    * This semaphore was initialized to SO_NAVI.
    */

   ports_and_ships_sync.sem_num = 1;
   ports_and_ships_sync.sem_op = 0;
   ports_and_ships_sync.sem_flg = 0;
   
   semop(sem_synch_id, &ports_and_ships_sync, 1);

   /* Simulation start */

   ports_and_ships_sync.sem_num = 2;
   ports_and_ships_sync.sem_op = my_config_variables.SO_NAVI + my_config_variables.SO_PORTI;
   ports_and_ships_sync.sem_flg = 0;

   alarm(my_config_variables.SO_DAYS);

   semop(sem_synch_id, &ports_and_ships_sync, 1);
   
   for(i=0; i<my_config_variables.SO_DAYS-1; i++) {
      nanosleep(&my_timeout, NULL);
      signal_to_everyone(SIGUSR2);
      check_global_offer();
      current_day++;
      print_stats();
   }
      
   /* Simulation ended*/
   for(i=0; i<my_config_variables.SO_NAVI + my_config_variables.SO_PORTI; i++) {
      waitpid(-1, NULL, 0);
   }

   free_existing_data_structures();
   return 0;
}

/*
 * This method is used to decide which configuration will be used during the simulation
 */
void choose_config() {
   int choice, cont = 0;
   do {
      printf("Please choose a configuration, digit the correspondent number:\n");
      printf("1. Lots of small ships, a few ports, lots of products\n");
      printf("2. Lots of big ships, a few ports, lots of products\n");
      printf("3. A few small ships, lots of ports, lots of products\n");
      printf("4. A few big ships, lots of ports, lots of products\n");
      printf("5. Lots of small ships, lots of ports, lots of products\n");
      printf("\nChoice: ");
      scanf("%d", &choice);
      printf("\n\n\n");

      switch (choice) {
         case 1:
            my_config_variables = setup_config_variables("file_config1.txt");
            cont = 1;
            break;

         case 2:
            my_config_variables = setup_config_variables("file_config2.txt");
            cont = 1;
            break;

         case 3:
            my_config_variables = setup_config_variables("file_config3.txt");
            cont = 1;
            break;

         case 4:
            my_config_variables = setup_config_variables("file_config4.txt");
            cont = 1;
            break;

         case 5:
            my_config_variables = setup_config_variables("file_config5.txt");
            cont = 1;
            break;
         
         default:
            printf("\t\tError, please digit a valid number (1-5)\n\n");
            break;
      }
   } while(!cont);
}

/*
 * This method reads from the given file and sets up the configuration variables
 * for the execution
 */
struct config_variables setup_config_variables(char* configuration_file) {
   FILE *file = fopen(configuration_file, "r");
   char buffer[100];
   char *var_name, *var_value;
   struct config_variables my_config_variables;

   if (file == NULL) {
      printf("Error while opening the file. \n");
   }

   while (fgets(buffer, sizeof(buffer), file)) {
      var_name = strtok(buffer, ":,");
      var_value = strtok(NULL, ":,");

      while (*var_name == ' ')
         var_name++;

      if (strcmp(var_name, "SO_NAVI") == 0)
         my_config_variables.SO_NAVI = atoi(var_value);
      else if (strcmp(var_name, "SO_PORTI") == 0)
         my_config_variables.SO_PORTI = atoi(var_value);
      else if (strcmp(var_name, "SO_MERCI") == 0)
         my_config_variables.SO_MERCI = atoi(var_value);
      else if (strcmp(var_name, "SO_SIZE") == 0)
         my_config_variables.SO_SIZE = atoi(var_value);
      else if (strcmp(var_name, "SO_MIN_VITA") == 0)
         my_config_variables.SO_MIN_VITA = atoi(var_value);
      else if (strcmp(var_name, "SO_MAX_VITA") == 0)
         my_config_variables.SO_MAX_VITA = atoi(var_value);
      else if (strcmp(var_name, "SO_LATO") == 0)
         my_config_variables.SO_LATO = atof(var_value);
      else if (strcmp(var_name, "SO_SPEED") == 0)
         my_config_variables.SO_SPEED = atof(var_value);
      else if (strcmp(var_name, "SO_CAPACITY") == 0)
         my_config_variables.SO_CAPACITY = atoi(var_value);
      else if (strcmp(var_name, "SO_BANCHINE") == 0)
         my_config_variables.SO_BANCHINE = atoi(var_value);
      else if (strcmp(var_name, "SO_FILL") == 0)
         my_config_variables.SO_FILL = atof(var_value);
      else if (strcmp(var_name, "SO_LOADSPEED") == 0)
         my_config_variables.SO_LOADSPEED = atoi(var_value);
      else if (strcmp(var_name, "SO_DAYS") == 0)
         my_config_variables.SO_DAYS = atoi(var_value);
   }
   fclose(file);

   return my_config_variables;
}

/*
 * This method handles every malloc and creation of the main ipc structures 
 */
void master_malloc_and_ipcs() {
   int i, local_shm_id;
   struct sembuf my_semops[3];

   /* Malloc and shm for ports infos */
   ports_infos = malloc(my_config_variables.SO_PORTI * sizeof(struct port_info));
   shm_id = shmget(IPC_PRIVATE, my_config_variables.SO_PORTI * sizeof(struct port_info), IPC_CREAT | 0666);
   ports_infos = (struct port_info *)shmat(shm_id, NULL, 0);
   
   for(i=0; i<my_config_variables.SO_PORTI; i++) {
      ports_infos[i].my_products_offer = malloc(my_config_variables.SO_MERCI * sizeof(struct product));
      local_shm_id = shmget(IPC_PRIVATE, my_config_variables.SO_MERCI * sizeof(struct product), IPC_CREAT | 0666);
      ports_infos[i].my_products_offer = (struct product *) shmat(local_shm_id, NULL, 0);
      ports_infos[i].off_shm_id = local_shm_id;

      ports_infos[i].my_products_demand = malloc(my_config_variables.SO_MERCI * sizeof(struct product));
      local_shm_id = shmget(IPC_PRIVATE, my_config_variables.SO_MERCI * sizeof(struct product), IPC_CREAT | 0666);
      ports_infos[i].my_products_demand = (struct product *) shmat(local_shm_id, NULL, 0);
      ports_infos[i].dem_shm_id = local_shm_id;
   }

   /* Mallocs and shm for stats */ 

   all_ships_stats = (int *)malloc(3 * sizeof(int));
   ship_stats_shm_id = shmget(IPC_PRIVATE, 3 * sizeof(int), IPC_CREAT | 0666);
   all_ships_stats = (int *)shmat(ship_stats_shm_id, NULL, 0);

   all_ports_stats = (struct port_stats *)malloc(my_config_variables.SO_PORTI * sizeof(struct port_stats));
   ports_stats_shm_id = shmget(IPC_PRIVATE, my_config_variables.SO_PORTI * sizeof(struct port_stats), IPC_CREAT | 0666);
   all_ports_stats = (struct port_stats *)shmat(ports_stats_shm_id, NULL, 0);
   
   all_products_stats = (struct prod_stats *)malloc(my_config_variables.SO_MERCI * sizeof(struct prod_stats));
   prod_stats_shm_id = shmget(IPC_PRIVATE, my_config_variables.SO_MERCI * sizeof(struct prod_stats), IPC_CREAT | 0666);
   all_products_stats = (struct prod_stats *)shmat(prod_stats_shm_id, NULL, 0);
   

   /* Synch sem setup */
   sem_synch_id = semget(IPC_PRIVATE, 3, 0600);

   bzero(my_semops,sizeof(my_semops));

   my_semops[0].sem_num = 0;
   my_semops[0].sem_op = my_config_variables.SO_PORTI;
   my_semops[1].sem_num = 1;
   my_semops[1].sem_op = my_config_variables.SO_NAVI;
   my_semops[2].sem_num = 2;
   my_semops[2].sem_op = 0;

   semop(sem_synch_id, my_semops, 3);
   
   /* Mallocs for ports and ships params */

   ship_params = malloc(SHIP_PARAMS_COUNT * sizeof(char *));

   for(i=0; i<SHIP_PARAMS_COUNT-1; i++) {
      ship_params[i] = malloc(20 * sizeof(char));
   }
   
   sprintf(ship_params[0], "%d", shm_id);
   sprintf(ship_params[1], "%d", sem_synch_id);
   sprintf(ship_params[2], "%d", my_config_variables.SO_PORTI);
   sprintf(ship_params[3], "%d", my_config_variables.SO_MERCI);
   sprintf(ship_params[4], "%f", my_config_variables.SO_LATO);
   sprintf(ship_params[5], "%f", my_config_variables.SO_SPEED);
   sprintf(ship_params[6], "%d", my_config_variables.SO_CAPACITY);
   sprintf(ship_params[7], "%f", my_config_variables.SO_LOADSPEED);
   sprintf(ship_params[8], "%d", ship_stats_shm_id);
   sprintf(ship_params[9], "%d", ports_stats_shm_id);
   sprintf(ship_params[10], "%d", prod_stats_shm_id);
   ship_params[11] = NULL;

   port_params = malloc(PORT_PARAMS_COUNT * sizeof(char *));  

   for(i=0; i<PORT_PARAMS_COUNT-1; i++) {
      port_params[i] = malloc(20 * sizeof(char));
   }
   
   sprintf(port_params[0], "%d", shm_id);
   sprintf(port_params[1], "%d", sem_synch_id);
   sprintf(port_params[2], "%d", my_config_variables.SO_PORTI);
   sprintf(port_params[3], "%d", my_config_variables.SO_MERCI);
   sprintf(port_params[4], "%d", my_config_variables.SO_SIZE);
   sprintf(port_params[5], "%d", my_config_variables.SO_MIN_VITA);
   sprintf(port_params[6], "%d", my_config_variables.SO_MAX_VITA);
   sprintf(port_params[7], "%d", my_config_variables.SO_BANCHINE);
   sprintf(port_params[8], "%d", (my_config_variables.SO_FILL / my_config_variables.SO_PORTI));
   sprintf(port_params[9], "%d", ports_stats_shm_id);
   sprintf(port_params[10], "%d", prod_stats_shm_id);
   port_params[12] = NULL;

   ports_pids = malloc(my_config_variables.SO_PORTI * sizeof(pid_t));
   ships_pids = malloc(my_config_variables.SO_NAVI * sizeof(pid_t));
}

void handle_signal(int signum) {
   int i=0;

   switch(signum) {
      case SIGALRM:
         current_day++;
         ended = 1;
         print_stats();
         signal_to_everyone(SIGUSR1);
         for(i=0; i<my_config_variables.SO_NAVI + my_config_variables.SO_PORTI; i++) {
            waitpid(-1, NULL, 0);
         }
         free_existing_data_structures();
         exit(EXIT_SUCCESS);
         break;
      case SIGINT:
         printf("\nMaster got SIGINT\n");
         ended = 1;
         print_stats();
         signal_to_everyone(SIGINT);
         for(i=0; i<my_config_variables.SO_NAVI + my_config_variables.SO_PORTI; i++) {
            waitpid(-1, NULL, 0);
         }
         free_existing_data_structures();
         printf("Done!\n\n\n");
         exit(EXIT_SUCCESS);
         break;
      case SIGTERM:
         printf("\nMaster got SIGTERM\n");
         ended = 1;
         print_stats();
         signal_to_everyone(SIGTERM);
         for(i=0; i<my_config_variables.SO_NAVI + my_config_variables.SO_PORTI; i++) {
            waitpid(-1, NULL, 0);
         }
         free_existing_data_structures();
         printf("Done!\n\n\n");
         exit(EXIT_SUCCESS);
         break;
      default:
         break;
   }
}

void signal_to_everyone(int signal) {
   int i;

   for(i=0; i<my_config_variables.SO_PORTI; i++) {
      kill(ports_pids[i], signal);
   }
   for(i=0; i<my_config_variables.SO_NAVI; i++) {
      kill(ships_pids[i], signal);
   }
}

void free_existing_data_structures() {
   int i, j;

   printf("\n\nMaster about to free the memory...\n");

   /* Mallocs free */
   free(ports_pids);
   free(ships_pids);

   for(i=0; i<PORT_PARAMS_COUNT-1; i++) {
      free(port_params[i]);
   }
   free(port_params);

   for(i=0; i<SHIP_PARAMS_COUNT-1; i++) {
      free(ship_params[i]);
   }
   free(ship_params);
   
   /* Ipcs free */

   for(i=0; i<my_config_variables.SO_PORTI; i++) {
      ports_infos[i].my_products_offer = (struct product *)shmat(ports_infos[i].off_shm_id, NULL, 0);
      ports_infos[i].my_products_demand = (struct product *)shmat(ports_infos[i].dem_shm_id, NULL, 0);
      for(j=0; j<my_config_variables.SO_MERCI; j++) {
         if(ports_infos[i].my_products_offer[j].product_semaphore != -1) {
            if(semctl(ports_infos[i].my_products_offer[j].product_semaphore, 0, IPC_RMID) == -1) {
               printf("Free error %d with sem %d at ind %d\n", 
                  ports_infos[i].port_pid, ports_infos[i].my_products_offer[j].product_semaphore, j);
            }
         }
         if(ports_infos[i].my_products_demand[j].product_semaphore != -1) {
            if(semctl(ports_infos[i].my_products_demand[j].product_semaphore, 0, IPC_RMID) == -1) {
               printf("Free error %d with sem %d at ind %d\n", 
                  ports_infos[i].port_pid, ports_infos[i].my_products_demand[j].product_semaphore, j);
            }
         }
      }
   }

   for(i=0; i<my_config_variables.SO_PORTI; i++) {
      shmdt(ports_infos[i].my_products_offer);
      shmdt(ports_infos[i].my_products_demand);

      shmctl(ports_infos[i].off_shm_id, IPC_RMID, NULL);
      shmctl(ports_infos[i].dem_shm_id, IPC_RMID, NULL);   

      semctl(ports_infos[i].quays_id, 0, IPC_RMID);
      msgctl(ports_infos[i].msg_queue_id, IPC_RMID, NULL);         
   }

   shmdt(all_ships_stats);
   shmctl(ship_stats_shm_id, IPC_RMID, NULL);

   shmdt(all_ports_stats);
   shmctl(ports_stats_shm_id, IPC_RMID, NULL);

   shmdt(all_products_stats);
   shmctl(prod_stats_shm_id, IPC_RMID, NULL);

   semctl(sem_synch_id, 0, IPC_RMID);
   semctl(sem_synch_id, 1, IPC_RMID);
   semctl(sem_synch_id, 2, IPC_RMID);

   shmdt(ports_infos);
   shmctl(shm_id, IPC_RMID, NULL);

   printf("Free completed successfully\n");
}

void print_stats() {
   int i;

   printf("\n\t\t\t\tSTATS ON DAY %d", current_day);

   /* Ship stats */
   printf("\n\nSHIPS STATS ON DAY %d", current_day);
   printf("\n\tEmpty: %d", all_ships_stats[0]);
   printf("\n\tLoaded: %d", all_ships_stats[1]);
   printf("\n\tIn port: %d", all_ships_stats[2]);
   printf("\n------------\n");

   /* Ports stats */
   printf("\n\nPORTS STATS ON DAY %d", current_day);
   for(i=0; i<my_config_variables.SO_PORTI; i++) {
      printf("\nPort %d, quays occupied: %d / %d", ports_infos[i].port_pid, 
         all_ports_stats[i].occupied_quays, all_ports_stats[i].total_quays);
      printf("\n\tTons available: %d", all_ports_stats[i].tons_available);
      printf("\n\tTons shipped: %d", all_ports_stats[i].tons_shipped);
      printf("\n\tTons delivered: %d", all_ports_stats[i].tons_delivered);
      printf("\n\tTons expired: %d\n", all_ports_stats[i].tons_expired);
   }
   printf("\n------------\n");

   /* Products stats */
   printf("\n\nPRODUCTS STATS ON DAY %d", current_day);
   for(i=0; i<my_config_variables.SO_MERCI; i++) {
      printf("\nProduct %d", i);
      printf("\n\tAvailable in ports: %d, Expired in ports: %d", all_products_stats[i].available_port, all_products_stats[i].expired_port);
      printf("\n\tOn ship: %d, Expired on a ship: %d", all_products_stats[i].on_ship, all_products_stats[i].expired_ship);
      printf("\n\tDelivered: %d", all_products_stats[i].delivered);
      if(ended) {
         printf("\n\tTop offering port: %d, Top demanding port: %d", 
            all_products_stats[i].top_offering_port, all_products_stats[i].top_demanding_port);
      }
   }
   printf("\n------------\n");
}

/* 
 * This method is used to find, for each product, the port that offered the most tons and
 * the port that demanded the most tons
 */
void find_best_ports() {
   int i, j, max_offer = 0, max_demand = 0;
   pid_t top_offering_port = 0, top_demanding_port = 0;

   for(i=0; i<my_config_variables.SO_MERCI; i++) {
      for(j=0; j<my_config_variables.SO_PORTI; j++) {
         ports_infos[j].my_products_offer = (struct product *)shmat(ports_infos[j].off_shm_id, NULL, 0);
         all_products_stats[i].available_port += ports_infos[j].my_products_offer[i].ton;
         if(ports_infos[j].my_products_offer[i].ton > max_offer) {
            max_offer = ports_infos[j].my_products_offer[i].ton;
            top_offering_port = ports_infos[j].port_pid;
         }
      }
      for(j=0; j<my_config_variables.SO_PORTI; j++) {
         ports_infos[j].my_products_demand = (struct product *)shmat(ports_infos[j].dem_shm_id, NULL, 0);
         if(ports_infos[j].my_products_demand[i].ton > max_demand) {
            max_demand = ports_infos[j].my_products_demand[i].ton;
            top_demanding_port = ports_infos[j].port_pid;
         }
      }
      all_products_stats[i].top_offering_port = top_offering_port;
      all_products_stats[i].top_demanding_port = top_demanding_port;

      max_offer = 0;
      max_demand = 0;
      top_offering_port = 0;
      top_demanding_port = 0;
   }
}

/*
 * This method is used to evaluate if it's necessary to end the simulation prematurely:
 * if no one is offering a product and there are no loaded ships the simulation will end
 */
void check_global_offer() {
   int i, j, count = 0;

   ports_infos = (struct port_info *)shmat(shm_id, NULL, 0);

   for(i=0; i<my_config_variables.SO_PORTI && count == 0; i++) {
      ports_infos[i].my_products_offer = (struct product *)shmat(ports_infos[i].off_shm_id, NULL, 0);
      for(j=0; j<my_config_variables.SO_MERCI && count == 0; j++) {
         if(ports_infos[i].my_products_offer[j].ton > 0) {
            count++;
         }
      }
   }

   all_ships_stats = (int *)shmat(ship_stats_shm_id, NULL, 0);

   if(count == 0 && all_ships_stats[1] == 0) {
      printf("\n\n\t\t\t\tSIMULATION ABOUT TO END DUE TO LACK OF OFFER \n\n\n\n");
      ended = 1;
      raise(SIGALRM);
   }
}
