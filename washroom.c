#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "uthread.h"
#include "uthread_mutex_cond.h"

#ifdef VERBOSE
#define VERBOSE_PRINT(S, ...) printf (S, ##__VA_ARGS__);
#else
#define VERBOSE_PRINT(S, ...) ;
#endif

#define MAX_OCCUPANCY      3
#define NUM_ITERATIONS     40
#define NUM_PEOPLE         40
#define FAIR_WAITING_COUNT 4

/**
 * You might find these declarations useful.
 */
enum GenderIdentity {MALE = 0, FEMALE = 1};
const static enum GenderIdentity otherGender [] = {FEMALE, MALE};

#define WAITING_HISTOGRAM_SIZE (NUM_ITERATIONS * NUM_PEOPLE)
int             entryTicker;                                          // incremented with each entry
int             waitingHistogram         [WAITING_HISTOGRAM_SIZE];
int             waitingHistogramOverflow;
uthread_mutex_t waitingHistogrammutex;
int             occupancyHistogram       [2] [MAX_OCCUPANCY + 1];


struct Washroom {
  // TODO
  int num_users;
  int user_gender;
  uthread_cond_t can_enter;
  int fair_count;
};

struct Washroom* createWashroom() {
  struct Washroom* washroom = malloc (sizeof (struct Washroom));
  // TODO
  washroom->num_users = 0;
  washroom->user_gender = MALE;
  washroom->can_enter = uthread_cond_create(waitingHistogrammutex) ;
  washroom->fair_count = 0;
  return washroom;
}

// A queue structure to explicitly record the waiting queue
struct Queue{
  int gender;
  struct Queue* next;
};

struct Queue* head =0;
struct Queue* tail =0;

struct Queue* top(){
  return head;
}

void enqueue(enum GenderIdentity g){
  if (!head){ 
    tail=head = malloc(sizeof(struct Queue));
    head->gender = g;
  }
  else{
    tail->next = malloc(sizeof(struct Queue));
    tail= tail->next;
    tail->gender = g;
  }
}

void dequeue(){
  if (!head) return;
  if (head == tail){
    struct Queue* temp = head;
    head = 0;
    free(temp);
  } else {
    struct Queue* temp = head;
    head = head->next;
    free(temp);
  }
}

struct Washroom* washroom;


int gateKeeper(enum GenderIdentity g , int act){
  assert(washroom->num_users >= 0 && washroom->num_users<=3);
  int ok_to_enter =0;
  switch(act){
    case 0:
    washroom->num_users--;
    if (head){
      if (washroom->num_users ==0){
        washroom->fair_count =0;
        uthread_cond_signal(washroom->can_enter);
        dequeue();
      }
      else if (washroom->user_gender == top()->gender){
        uthread_cond_signal(washroom->can_enter);
        occupancyHistogram[washroom->user_gender] [washroom->num_users] ++;
        dequeue();
      }
      occupancyHistogram[washroom->user_gender] [washroom->num_users] ++;
    }
    return 1;
    break;

    case 1:
    if (washroom->num_users == 0) {
      washroom->num_users++;
      washroom->user_gender = g;
      occupancyHistogram[g] [washroom->num_users] ++;
      return 1;
    }
    else if (washroom->num_users == 3) return 0;
    else if (washroom->user_gender == g){
      if(!head || (head && washroom->fair_count<FAIR_WAITING_COUNT) ){
        washroom->fair_count++;
        washroom->num_users++;
        occupancyHistogram[g] [washroom->num_users] ++;
        return 1;
      }
      else return 0;
    } 
    else return 0;
    break;
  }
}

void enterWashroom (enum GenderIdentity g) {
  uthread_mutex_lock(waitingHistogrammutex);
  while(!gateKeeper(g,1)){
    enqueue(g);
    uthread_cond_wait(washroom->can_enter);
  }
  entryTicker++;
  uthread_mutex_unlock(waitingHistogrammutex);
}

void leaveWashroom() {
  uthread_mutex_lock(waitingHistogrammutex);
  gateKeeper(MALE,0);
  uthread_mutex_unlock(waitingHistogrammutex);
  for (int i = 0; i <30* NUM_ITERATIONS; i++){
    uthread_yield();
  }
}


void recordWaitingTime (int waitingTime) {
  uthread_mutex_lock (waitingHistogrammutex);
  if (waitingTime < WAITING_HISTOGRAM_SIZE)
    waitingHistogram [waitingTime] ++;
  else
    waitingHistogramOverflow ++;
  uthread_mutex_unlock (waitingHistogrammutex);
}

//
// TODO
// You will probably need to create some additional produres etc.
//

void* goTOWashRoom(void* av){
  enum GenderIdentity a = (enum GenderIdentity) av;
  for(int i = 0 ; i< NUM_ITERATIONS;i++){
    int initial_wait = entryTicker;
    enterWashroom(a);
    int final_wait = entryTicker;
    recordWaitingTime(final_wait - initial_wait - 1);
    for (int i = 0; i < NUM_ITERATIONS; i++){
      uthread_yield();
    }
    leaveWashroom();
  }
}

int main (int argc, char** argv) {
  uthread_init (1);
  uthread_t pt [NUM_PEOPLE];
  waitingHistogrammutex = uthread_mutex_create ();
  washroom = createWashroom();
  srand(time(NULL));
  // TODO
  int g[] = {MALE,FEMALE};
  for (int i = 0; i < NUM_PEOPLE; i++){

    int r = random() % 2;
    enum GenderIdentity gender = g[r];
    pt[i] = uthread_create(goTOWashRoom,(void*)gender);
  }
  for (int i=0;i<NUM_PEOPLE;i++)
    uthread_join(pt[i],0);

  printf ("Times with 1 person who identifies as male   %d\n", occupancyHistogram [MALE]   [1]);
  printf ("Times with 2 people who identifies as male   %d\n", occupancyHistogram [MALE]   [2]);
  printf ("Times with 3 people who identifies as male   %d\n", occupancyHistogram [MALE]   [3]);
  printf ("Times with 1 person who identifies as female %d\n", occupancyHistogram [FEMALE] [1]);
  printf ("Times with 2 people who identifies as female %d\n", occupancyHistogram [FEMALE] [2]);
  printf ("Times with 3 people who identifies as female %d\n", occupancyHistogram [FEMALE] [3]);
  printf ("Waiting Histogram\n");
  for (int i=0; i<WAITING_HISTOGRAM_SIZE; i++)
    if (waitingHistogram [i])
      printf ("  Number of times people waited for %d %s to enter: %d\n", i, i==1?"person":"people", waitingHistogram [i]);
    if (waitingHistogramOverflow)
      printf ("  Number of times people waited more than %d entries: %d\n", WAITING_HISTOGRAM_SIZE, waitingHistogramOverflow);
  }
