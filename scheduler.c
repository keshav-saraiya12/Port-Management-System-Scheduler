#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>
 
 #define MAX_DOCKS 30
 #define MAX_CARGO_COUNT 200
 #define MAX_AUTH_STRING_LEN 100
 #define MAX_NEW_REQUESTS 100
 #define MAX_SOLVERS 8
 #define MAX_CRANES 25
 #define MAX_CATEGORY 25
 #define MAX_SHIPS 2000
 #define MAX_STRING_CHARS 6
 
 #define MSG_TYPE_NEW_REQUEST 1
 #define MSG_TYPE_DOCK 2
 #define MSG_TYPE_UNDOCK 3
 #define MSG_TYPE_MOVE_CARGO 4
 #define MSG_TYPE_END_TIMESTEP 5
 
 #define SOLVER_MSG_SET_DOCK 1
 #define SOLVER_MSG_GUESS 2
 #define SOLVER_MSG_RESPONSE 3
 
 
 typedef struct {
     int originalCraneId;
     int capacity;
     bool isUsed;
 } Crane;
 
 typedef struct {
     int originalDockId;
     int category;
     int numCranes;
     Crane cranes[MAX_CRANES];
     bool isOccupied;
     int occupyingShipId;
     int occupyingShipDirection;
     int cargoDoneAt;  
     bool undockingDone; 
     int dockedAt;
     int cargoMovedTill;
     int numCargodoc;
 } Dock;
 
 typedef struct {
  int weight;
  int originalIndex;
  bool used;
} CargoItem;


 typedef struct {
     int shipId;
     int arrivalTime;
     int category;
     int direction;
     int emergency;
     int waitingTime;
     int cutoffTime;
     int numCargo;
     int cargoMovedTill;
     CargoItem cargo[MAX_CARGO_COUNT];
     bool isDocked;
     int assignedDockId;
     int dockedAt;
 } Ship;
 
 typedef struct {
     long mtype;
     int timestep;
     int shipId;
     int direction;
     int dockId;
     int cargoId;
     int isFinished;
     union {
         int numShipRequests;
         int craneId;
     } data;
 } MessageStruct;

 typedef struct {
  int shipId;
  int timestep;
  int category;
  int direction;
  int emergency;
  int waitingTime;
  int numCargo;
  int cargo[MAX_CARGO_COUNT];
} ShipRequest;

 
 typedef struct {
     char authStrings[MAX_DOCKS][MAX_AUTH_STRING_LEN];
     ShipRequest newShipRequests[MAX_NEW_REQUESTS];
 } MainSharedMemory;
 
 typedef struct {
     long mtype;
     int dockId;
     char authStringGuess[MAX_AUTH_STRING_LEN];
 } SolverRequest;
 
 typedef struct {
     long mtype;
     int guessIsCorrect;
 } SolverResponse;
 
 
 #define MAX_PREFIXES_PER_THREAD 6 

 typedef struct {
     int solverId;
     int dockId;
     int strLen;
     int shipId;
     int direction;
     int numPrefixes;
     char multiPrefixes[MAX_PREFIXES_PER_THREAD][3];  
 } ThreadArgs;


const char *prefixSets[7][MAX_SOLVERS][MAX_PREFIXES_PER_THREAD] = {
    // ns = 2
    {
        {"5", "6", "7",NULL},  // solver 0
        {"8", "9",NULL}        // solver 1
    },

    // ns = 3
    {
        {"5", "6",NULL},       // solver 0
        {"7", "8",NULL},       // solver 1
        {"9",NULL}             // solver 2
    },

    // ns = 4
    {
        {"5", "6",NULL},       // solver 0
        {"7",NULL},            // solver 1
        {"8",NULL},            // solver 2
        {"9",NULL}             // solver 3
    },

    // ns = 5
    {
        {"5",NULL}, {"6",NULL}, {"7",NULL}, {"8",NULL}, {"9",NULL}
    },

    // ns = 6
    {
        {"55", "56", "57", "58", "59",NULL},
        {"5.", "65", "66", "67", "68",NULL},
        {"69", "6.", "75", "76", "77",NULL},
        {"78", "79", "7.", "85", "86",NULL},
        {"87", "88", "89", "8.", "95",NULL},
        {"96", "97", "98", "99", "9.",NULL}
    },

    // ns = 7
    {
        {"55", "56", "57", "58", "59",NULL},
        {"5.", "65", "66", "67", "68",NULL},
        {"69", "6.", "75", "76",NULL},
        {"77", "78", "79", "7.",NULL},
        {"85", "86", "87", "88",NULL},
        {"89", "8.", "95", "96",NULL},
        {"97", "98", "99", "9.",NULL}
    },

    // ns = 8
    {
        {"55", "56", "57", "58",NULL},
        {"59", "5.", "65", "66",NULL},
        {"67", "68", "69", "6.",NULL},
        {"75", "76", "77", "78",NULL},
        {"79", "7.", "85", "86",NULL},
        {"87", "88", "89", "8.",NULL},
        {"95", "96", "97",NULL},
        {"98", "99", "9.",NULL}
    }
};



 int shmKey, mainMsgKey, numSolvers;
 int solverMsgKeys[MAX_SOLVERS];
 int shmId, mainMsgId, solverMsgIds[MAX_SOLVERS];
 MainSharedMemory *sharedMemory;
 Dock docks[MAX_DOCKS];
 int numDocks;

 volatile bool authStringFound[MAX_DOCKS] = { false };

 Ship emergencyIncoming[100];    
 Ship regularIncoming[1000];    
 Ship outgoingShips[500];   

 int emergencyIncomingCount = 0;
 int regularIncomingCount = 0;
 int outgoingCount = 0;
 

 int numShips;
 int currentTimestep = 1;

 int compareDocksByCategory(const void *a, const void *b) {
  Dock *d1 = (Dock *)a;
  Dock *d2 = (Dock *)b;
  return d1->category - d2->category;
 }

 int compareCranesByCapacity(const void *a, const void *b) {
  Crane *c1 = (Crane *)a;
  Crane *c2 = (Crane *)b;
  return c2->capacity - c1->capacity;
 }
 
 void initIPC(int testCase) {
     char path[256];
     sprintf(path, "testcase%d/input.txt", testCase);
     FILE *fp = fopen(path, "r");
     if (!fp) {
         perror("Error opening input.txt");
         exit(1);
     }
 
     fscanf(fp, "%d", &shmKey);
     fscanf(fp, "%d", &mainMsgKey);
     fscanf(fp, "%d", &numSolvers);
     for (int i = 0; i < numSolvers; i++) {
         fscanf(fp, "%d", &solverMsgKeys[i]);
     }
 
     fscanf(fp, "%d", &numDocks);
     for (int i = 0; i < numDocks; i++) {
         fscanf(fp, "%d", &docks[i].category);
         docks[i].originalDockId = i;
         docks[i].numCranes = docks[i].category;
         docks[i].isOccupied = false;
         for (int j = 0; j < docks[i].numCranes; j++) {
             int capacity;
             fscanf(fp, "%d", &capacity);
             docks[i].cranes[j].originalCraneId = j;
             docks[i].cranes[j].capacity = capacity;
             docks[i].cranes[j].isUsed = false;
         }

        qsort(docks[i].cranes, docks[i].numCranes, sizeof(Crane), compareCranesByCapacity);
     }
     fclose(fp);


     qsort(docks, numDocks, sizeof(Dock), compareDocksByCategory);
 
     shmId = shmget(shmKey, sizeof(MainSharedMemory), 0666);
     if (shmId == -1) {
         perror("shmget");
         exit(1);
     }
     sharedMemory = (MainSharedMemory*) shmat(shmId, NULL, 0);
     if (sharedMemory == (void*)-1) {
         perror("shmat");
         exit(1);
     }
 
     mainMsgId = msgget(mainMsgKey, 0666);
     if (mainMsgId == -1) {
         perror("msgget main");
         exit(1);
     }
 
     for (int i = 0; i < numSolvers; ++i) {
         solverMsgIds[i] = msgget(solverMsgKeys[i], 0666);
         if (solverMsgIds[i] == -1) {
             perror("msgget solver");
             exit(1);
         }
     }
 }
 
 void assignShipsToDocks(Ship *shipArray, int shipCount) {
  for (int i = 0; i < shipCount; ++i) {
      Ship *ship = &shipArray[i];
      if (ship->isDocked || ship->direction == 0) continue;
      if ((ship->direction==1) && (ship->emergency==0) && (currentTimestep>ship->cutoffTime)) continue;

      for (int j = 0; j < numDocks; ++j) {
          Dock *dock = &docks[j];
          if (!dock->isOccupied && dock->category >= ship->category) {
              ship->isDocked = true;
              ship->dockedAt = currentTimestep;
              
              ship->assignedDockId = dock->originalDockId;
              dock->isOccupied = true;
              dock->dockedAt = currentTimestep;
              dock->occupyingShipId = ship->shipId;
              dock->occupyingShipDirection = ship->direction;
              dock->numCargodoc = ship->numCargo;

              MessageStruct msg;
              msg.mtype = MSG_TYPE_DOCK;
              msg.shipId = ship->shipId;
              msg.direction = ship->direction;
              msg.dockId = dock->originalDockId;
              msgsnd(mainMsgId, &msg, sizeof(MessageStruct) - sizeof(long), 0);
              break;
          }
      }
  }
}

 
void performCargoAssignment(Ship *shipArray, int count)
{
    for (int d = 0; d < numDocks; ++d) {
      Dock *dock = &docks[d];
      if (!dock->isOccupied) continue;

      for (int s = 0; s < count; ++s) {
      
          Ship *ship = &shipArray[s];

      if (!ship->isDocked || ship->assignedDockId != dock->originalDockId)
          continue;
      
      if (ship->shipId != dock->occupyingShipId || ship->direction != dock->occupyingShipDirection)
        continue;
            
                

   
      if (currentTimestep <= ship->dockedAt)
          continue;

        
        if(ship->cargoMovedTill<ship->numCargo)
        {
          for(int i = 0; i < dock->numCranes; i++)
          {
              Crane *crane = &dock->cranes[i];
              bool assigned = false;
              for(int j=0;j < ship->numCargo;j++)
              {
                
                if(!ship->cargo[j].used && crane->capacity >= ship->cargo[j].weight)
                {
                  MessageStruct msg;
                  msg.mtype = MSG_TYPE_MOVE_CARGO;
                  msg.shipId = ship->shipId;
                  msg.direction = ship->direction;
                  msg.dockId = dock->originalDockId;
                  msg.cargoId = ship->cargo[j].originalIndex;
                  msg.data.craneId = crane->originalCraneId;

                  msgsnd(mainMsgId, &msg, sizeof(MessageStruct) - sizeof(long), 0);

                  ship->cargo[j].used = true;
                  ship->cargoMovedTill++;
                  dock->cargoMovedTill++;
                  assigned = true;
                  break;
                }
              }

              if(ship->cargoMovedTill == ship->numCargo)
              {
                dock->cargoDoneAt = currentTimestep;
                break;
              }

              if (!assigned) break;
          }
        }
      }
  }

}

 
void* guessAuthStringThread(void *arg) {
  ThreadArgs *args = (ThreadArgs*) arg;
  int solverId = args->solverId;
  int dockId = args->dockId;
  int strLen = args->strLen;

  SolverRequest setDockMsg = { .mtype = SOLVER_MSG_SET_DOCK, .dockId = dockId };
  msgsnd(solverMsgIds[solverId], &setDockMsg, sizeof(SolverRequest) - sizeof(long), 0);

  char guess[MAX_AUTH_STRING_LEN];
  guess[strLen] = '\0';

  const char validChars[] = {'5', '6', '7', '8', '9', '.'};
  int validCharCount = 6;

  int prefixLen = strlen(args->multiPrefixes[0]); 

  int middleStart = prefixLen;
  int middleLen = strLen - middleStart - 1;

  long long totalCombos = 1;
  for (int i = 0; i < middleLen; ++i) totalCombos *= validCharCount;

  for (int p = 0; p < args->numPrefixes; ++p) {
      strncpy(guess, args->multiPrefixes[p], prefixLen);

      for (int last = 0; last < 5; ++last) {
          guess[strLen - 1] = validChars[last];

         
          for (long long combo = 0; combo < totalCombos; ++combo) {
            if (authStringFound[dockId]) pthread_exit(NULL);

            long long temp = combo;
            for (int pos = middleStart; pos < strLen - 1; ++pos) {
                guess[pos] = validChars[temp % validCharCount];
                temp /= validCharCount;
            }

            SolverRequest guessMsg = {
                .mtype = SOLVER_MSG_GUESS,
                .dockId = dockId
            };
            strncpy(guessMsg.authStringGuess, guess, MAX_AUTH_STRING_LEN);
            msgsnd(solverMsgIds[solverId], &guessMsg, sizeof(SolverRequest) - sizeof(long), 0);

            SolverResponse response;
            msgrcv(solverMsgIds[solverId], &response, sizeof(SolverResponse) - sizeof(long), SOLVER_MSG_RESPONSE, 0);

            if ((response.guessIsCorrect == 1) && (authStringFound[dockId]==false)) {
                authStringFound[dockId] = true;
                strncpy(sharedMemory->authStrings[dockId], guess, MAX_AUTH_STRING_LEN);

                MessageStruct msg;
                msg.mtype = MSG_TYPE_UNDOCK;
                msg.shipId = args->shipId;
                msg.direction = args->direction;
                msg.dockId = dockId;
                msgsnd(mainMsgId, &msg, sizeof(MessageStruct) - sizeof(long), 0);

                pthread_exit(NULL);
            }
        }
    }
}

pthread_exit(NULL);
}



void getPrefixBucket(int ns, int solverId, char bucket[][3], int *count) {
    *count = 0;

    if (ns < 2 || ns > 8 || solverId >= ns) {
        return;  
    }

    int index = ns - 2; 
    int j = 0;

    

    while (prefixSets[index][solverId][j] != NULL) 
    {
        strncpy(bucket[j], prefixSets[index][solverId][j], 3);
        j++;
    }
    *count = j;
}

void* fastGuessLen1Thread(void *arg) {
    ThreadArgs *args = (ThreadArgs*) arg;
    int solverId = args->solverId;
    int dockId = args->dockId;

    SolverRequest setDockMsg = { .mtype = SOLVER_MSG_SET_DOCK, .dockId = dockId };
    msgsnd(solverMsgIds[solverId], &setDockMsg, sizeof(SolverRequest) - sizeof(long), 0);

    const char validFirstChars[] = {'5', '6', '7', '8', '9'};

    for (int i = 0; i < 5; ++i) {
        char guess[2];
        guess[0] = validFirstChars[i];
        guess[1] = '\0';

        SolverRequest guessMsg = {
            .mtype = SOLVER_MSG_GUESS,
            .dockId = dockId
        };
        strncpy(guessMsg.authStringGuess, guess, MAX_AUTH_STRING_LEN);
        msgsnd(solverMsgIds[solverId], &guessMsg, sizeof(SolverRequest) - sizeof(long), 0);

        SolverResponse response;
        msgrcv(solverMsgIds[solverId], &response, sizeof(SolverResponse) - sizeof(long), SOLVER_MSG_RESPONSE, 0);

        if ((response.guessIsCorrect == 1) && (authStringFound[dockId]==false)) {
            authStringFound[dockId] = true;
            strncpy(sharedMemory->authStrings[dockId], guess, MAX_AUTH_STRING_LEN);

            MessageStruct msg;
            msg.mtype = MSG_TYPE_UNDOCK;
            msg.shipId = args->shipId;
            msg.direction = args->direction;
            msg.dockId = dockId;

            msgsnd(mainMsgId, &msg, sizeof(MessageStruct) - sizeof(long), 0);
            pthread_exit(NULL);
        }
    }

    pthread_exit(NULL);
}


void performUndocking() 
{
  pthread_t threads[MAX_SOLVERS];
  ThreadArgs args[MAX_SOLVERS];

  for (int d = 0; d < numDocks; ++d) 
  {
      Dock *dock = &docks[d];
      if (!dock->isOccupied || dock->undockingDone) continue;

      if (dock->cargoDoneAt + 1 == currentTimestep) 
      {
          if (dock->cargoMovedTill < dock->numCargodoc) {
              continue;
          }

          int strLen = dock->cargoDoneAt - dock->dockedAt;
          authStringFound[dock->originalDockId] = false;

          if (strLen == 1) {
            args[0].solverId = 0;
            args[0].dockId = dock->originalDockId;
            args[0].strLen = strLen;
            args[0].shipId = dock->occupyingShipId;
            args[0].direction = dock->occupyingShipDirection;
        
            pthread_create(&threads[0], NULL, fastGuessLen1Thread, &args[0]);
            pthread_join(threads[0], NULL);
            dock->undockingDone = true;
            continue; 
         }

          char validPrefixes[] = { '5', '6', '7', '8', '9' };
          int totalPrefixes = 5;

          int base = totalPrefixes / numSolvers;
          int extra = totalPrefixes % numSolvers;
          int prefixIdx = 0;

          for (int i = 0; i < numSolvers; ++i) {
             char bucket[MAX_PREFIXES_PER_THREAD][3];
             int bucketCount = 0;
             
             int checkval = numSolvers;

             getPrefixBucket(numSolvers, i, bucket, &bucketCount);

              args[i].solverId = i;
              args[i].dockId = dock->originalDockId;
              args[i].strLen = strLen;
              args[i].shipId = dock->occupyingShipId;               
              args[i].direction = dock->occupyingShipDirection;         
              args[i].numPrefixes = bucketCount;


              for (int j = 0; j < bucketCount; ++j) {
                strncpy(args[i].multiPrefixes[j], bucket[j], 3);
                args[i].multiPrefixes[j][2] = '\0';
            }

              pthread_create(&threads[i], NULL, guessAuthStringThread, &args[i]);
          }

          dock->undockingDone = true;

          for (int i = 0; i < numSolvers; ++i)
              pthread_join(threads[i], NULL);
      }
  }
}


    int compareCargoByWeight(const void *a, const void *b) {
    CargoItem *c1 = (CargoItem *)a;
    CargoItem *c2 = (CargoItem *)b;
    return c2->weight - c1->weight;
    }

    int compareShipsByCutoffTime(const void *a, const void *b) {
    const Ship *s1 = (const Ship *)a;
    const Ship *s2 = (const Ship *)b;
    return s1->cutoffTime - s2->cutoffTime;
    }

    int compareShipsByArrivalTime(const void *a, const void *b) {
    const Ship *s1 = (const Ship *)a;
    const Ship *s2 = (const Ship *)b;
    return s1->arrivalTime - s2->arrivalTime;
    }

    int findShipIndex(Ship *arr, int count, int shipId, int direction) {
    for (int i = 0; i < count; ++i) {
        if (arr[i].shipId == shipId && arr[i].direction == direction)
            return i;
    }
    return -1;
    }


 
void handleTimestep(MessageStruct msg)
{
    
      for (int d = 0; d < numDocks; ++d) {
        Dock *dock = &docks[d];

        if (dock->isOccupied && dock->undockingDone && dock->cargoDoneAt + 2 == currentTimestep) {
            dock->isOccupied = false;
            dock->occupyingShipId = -1;
            dock->occupyingShipDirection = 0;
            dock->undockingDone = false;
            dock->cargoDoneAt=0;
            dock->cargoMovedTill=0;
            dock->numCargodoc=0;
        }

        for (int c = 0; c < dock->numCranes; ++c) {
            dock->cranes[c].isUsed = false;
        }
      }
    
     
      int newShipCount = msg.data.numShipRequests;
       
      for (int i = 0; i < newShipCount; ++i) 
      {
          ShipRequest *req = &sharedMemory->newShipRequests[i];
          Ship *ship;
          
          if (req->direction == 1 && req->emergency == 1) {
            int idx = findShipIndex(emergencyIncoming, emergencyIncomingCount, req->shipId, req->direction);
            if (idx == -1)
                idx = emergencyIncomingCount++;
            ship = &emergencyIncoming[idx];
          }
          else if (req->direction == 1 && req->emergency == 0) {
              int idx = findShipIndex(regularIncoming, regularIncomingCount, req->shipId, req->direction);
              if (idx == -1)
                  idx = regularIncomingCount++;
              ship = &regularIncoming[idx];
          }
          else if (req->direction == -1) {
              int idx = findShipIndex(outgoingShips, outgoingCount, req->shipId, req->direction);
              if (idx == -1)
                  idx = outgoingCount++;
              ship = &outgoingShips[idx];
          }
        
          
          ship->shipId = req->shipId;
          ship->arrivalTime = req->timestep;
          ship->category = req->category;
          ship->direction = req->direction;
          ship->emergency = req->emergency;
          ship->waitingTime = req->waitingTime;
          ship->cutoffTime = req->timestep + req->waitingTime;
          ship->numCargo = req->numCargo;
          ship->isDocked = false;
          ship->assignedDockId = -1;
          ship->cargoMovedTill=0;

  
          for (int j = 0; j < ship->numCargo; ++j) {
              ship->cargo[j].weight = req->cargo[j];
              ship->cargo[j].originalIndex = j;
          }

          qsort(ship->cargo, ship->numCargo, sizeof(CargoItem), compareCargoByWeight);
      }

      qsort(emergencyIncoming, emergencyIncomingCount, sizeof(Ship), compareShipsByCutoffTime);
      qsort(regularIncoming, regularIncomingCount, sizeof(Ship), compareShipsByCutoffTime);
      qsort(outgoingShips, outgoingCount, sizeof(Ship), compareShipsByArrivalTime); // new comparator
   
     assignShipsToDocks(emergencyIncoming, emergencyIncomingCount);
     assignShipsToDocks(regularIncoming, regularIncomingCount);
     assignShipsToDocks(outgoingShips,outgoingCount);
     
    performCargoAssignment(emergencyIncoming, emergencyIncomingCount);
    performCargoAssignment(regularIncoming, regularIncomingCount);
    performCargoAssignment(outgoingShips, outgoingCount);


     performUndocking();
 
     MessageStruct endMsg = {.mtype = MSG_TYPE_END_TIMESTEP};
     msgsnd(mainMsgId, &endMsg, sizeof(MessageStruct) - sizeof(long), 0);
 }
 
 int main(int argc, char *argv[]) {
     if (argc < 2) {
         fprintf(stderr, "Usage: %s <test_case_number>\n", argv[0]);
         exit(1);
     }
 
     int testCase = atoi(argv[1]);
    
 
     initIPC(testCase);

     
     while (1) {
         MessageStruct msg;
         if (msgrcv(mainMsgId, &msg, sizeof(MessageStruct) - sizeof(long), MSG_TYPE_NEW_REQUEST, 0) == -1) {
             perror("msgrcv");
             exit(1);
         }
        
         
 
         if (msg.isFinished == 1) {
             break;
         }
         
         currentTimestep = msg.timestep;
        
         handleTimestep(msg);
     }
 
     return 0;
 }
