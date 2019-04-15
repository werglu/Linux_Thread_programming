#include <stdlib.h>
#include <stddef.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#define MAXLINE 4096
#define DEFAULT_THREADCOUNT 10
#define DEFAULT_SAMPLESIZE 100
#define ERR(source) (perror(source),\
                     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     exit(EXIT_FAILURE))
//Program wielowatkowy sortujacy tablice. Na wejsciu podajemy n i t, gdzie n 
// to wielkosc tablicy, a t to ilosc roboczych watkow. Watek glowny tworzy
// tablice inicjalizujac ja wartosciami losowymi z przedzialu [0, 512]
// oraz tworzy dwa watki P i S. Glowny watek oczekuje na zakonczenie sie watkow S i P.
// Watek P tworzy t watkow R i dziala w petli: wypisuje aktualny stan tablicy, sprawdza czy istnieje jakis
// aktywny watek R i idzie spac na 2 sekundy. Jesli nie ma juz zadnych aktywnych watkow R, watek P czeka na
// zakonczenie sie wszystkich watkow R i konczy sie.
// Watek S obsluguje sygnaly. Na SIGINT anuluje jeden, kolejny watek R i sprawdza czy tablica jest juz posortowana.
// Watek S konczy sie jesli tablica jest juz posortowana, jesli nie ma juz aktywnych watkow R albo
// jesli przyszedl sygnal SIGQUIT. Jesli watek S zakonczyl sie poprzez SIGQUIT to glowny watek
// anuluje watek P, czeka na jego zakonczenie i konczy proces.
//
//Kazdy watek R dzia³a w petli: wybiera losowy przedzial [a, b] tablicy, znajduje najwieksza wartosc
// w tym przedziale, i zamienia ja z wartoscia na pozycji [b]. Na koncu petli watek idzie spac
// na losowo wybrany czas z przedzialu 500-1500ms. Watki R posiadaja zmienna informujaca o jego stanie:
// Active lub Inactive. Watki R tworzone sa ze stanem Active, jesli zostanie anulowany lub jesli sie zakonczy to
// zmienia swoj stan na Inactive. Kazdy dostep (zapis, odczyt) do stanu watkow R
// musi byc zabezpieczony unikalnym dla niego mutexxem.


typedef struct timespec timespec_t;
typedef unsigned int UINT;
typedef struct argsEstimation {
        pthread_t tid;
        UINT seed;
        int t;
        int* tab;
        int n;
} args_t;

typedef struct argss {
        pthread_t tid;
        UINT seed;
        int t;
        int* tab;
        int n;
        sigset_t *pMask;
} args_s;
typedef struct argsw {
        pthread_t tid;
        UINT seed;
        int t;
        int* tab;
        pthread_mutex_t *pmxtab;
        int n;
        bool stan; //active = true;
        pthread_mutex_t *pmxstan;
} args_w;

void msleep(UINT milisec) {
    time_t sec= (int)(milisec/1000);
    milisec = milisec - (sec*1000);
    timespec_t req= {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if(nanosleep(&req,&req)) ERR("nanosleep");
}

void* funkcja_watku(void *arg)
{
    args_w* args = arg;
   printf("tid  R: %ld\n", args->tid);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
   int a, b, czas;
   while(1){
   b = (int) (((double)rand_r(&args->seed)/(double)(RAND_MAX))*args->n);
   a = (int) (((double)rand_r(&args->seed)/(double)(RAND_MAX))*b);
   czas = (int) (500+((double)rand_r(&args->seed)/(double)(RAND_MAX))*1001);
   int min = 256;
   int poz=a;
   for (int i=a; i<=b; i++)
   {   
       pthread_mutex_lock(args->pmxtab);
       if (args->tab[i]<min) 
        {
            min=args->tab[i];
           
            poz=i;
        }
        pthread_mutex_unlock(args->pmxtab);
   }
   pthread_mutex_lock(args->pmxtab);
   int tmp=args->tab[poz];
   args->tab[poz]=args->tab[a];
   args->tab[a]=tmp;
   pthread_mutex_unlock(args->pmxtab);
   msleep(czas);
   }
   pthread_mutex_lock(args->pmxstan);
   args->stan=0;
   pthread_mutex_unlock(args->pmxstan);
   return NULL;
}

void* funkcja_p(void *arg)
{
    args_t* args = arg;
    args_w* watki = (args_w*) malloc(sizeof(args_w) * args->t);
    if (watki == NULL) ERR("Malloc error for watki arguments!");
     printf("tid  P: %ld\n", args->tid);
    pthread_mutex_t mxstan = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mxtab = PTHREAD_MUTEX_INITIALIZER;
   
    for (int i = 0; i < args->t; i++) {
                watki[i].seed = rand();
                watki[i].t = args->t;
                watki[i].n = args->n;
                watki[i].tab=args->tab;
                watki[i].stan=1;
                watki[i].pmxstan=&mxstan;
                watki[i].pmxtab=&mxtab;
        }
    for (int i = 0; i < args->t; i++) {
                int err = pthread_create(&(watki[i].tid), NULL, funkcja_watku, &watki[i]);
                if (err != 0) ERR("Couldn't create thread");
        }

    for (int i=0; i<args->n; i++)
    {
        pthread_mutex_lock(&mxtab);
        printf("%d ", args->tab[i]);
        pthread_mutex_unlock(&mxtab);
    }
    printf("\n");
    int ilea=0;
    
    while (1)
    { 
        ilea=0;
         for (int i=0; i<args->n; i++)
         {
             pthread_mutex_lock(&mxtab);
             printf("%d ", args->tab[i]);
             pthread_mutex_unlock(&mxtab);
         }
        printf("\n");
        for (int i = 0; i < args->t; i++) {
             pthread_mutex_lock(&mxstan);
             if (watki[i].stan==1) ilea++;
             pthread_mutex_unlock(&mxstan);
        }
        if(ilea==0) break;
        
        msleep(2000); 
    }
     
    for (int i = 0; i < args->t; i++) {
                int err = pthread_join(watki[i].tid, NULL);
                if (err != 0) ERR("Can't join with a thread");
               
        }

 for (int i=0; i<args->n; i++)
    {
        pthread_mutex_lock(&mxtab);
        printf("%d ", args->tab[i]);
        pthread_mutex_unlock(&mxtab);
    }

    printf("\n");
    free(watki);
    return NULL;
}

void* funkcja_s(void *arg)
{
    args_s* args = arg;
    int sig;
    srand(time(NULL));
    for (;;)
    { int i=0;
        if(sigwait(args->pMask, &sig)) ERR("sigwait");
        {
            if(sig==SIGINT)
            {
                pthread_cancel(watki[i].tid);
                i++;
                
            }
        }
    }
    return NULL;
}

int main(int argc, char** argv) {
        int n, t;

        n=atoi(argv[1]);
        t=atoi(argv[2]);
        args_t P;
        args_s S;
        srand(time(NULL));
        int * tab = (int*)malloc(sizeof(int)*n);
        for (int i=0; i<n; i++)
        {
            tab[i]= (int)(rand()%225);
            printf("%d ", tab[i]);
        }
        printf("\n");
        sigset_t oldMask, newMask;
        sigemptyset(&newMask);
        sigaddset(&newMask, SIGINT);
       
        if (pthread_sigmask(SIG_BLOCK, &newMask, &oldMask)) ERR("SIG_BLOCK error");

        P.seed = rand();
        P.t=t;
        P.tab=tab;
        P.n=n;
        pthread_create(&(P.tid), NULL, funkcja_p, &P);
        S.seed = rand();
        S.t=t;
        S.tab=tab;
        S.n=n;
        S.pMask=&newMask;
         pthread_create(&(S.tid), NULL, funkcja_s, &S);

        int err = pthread_join(P.tid, NULL);
        if (err != 0) ERR("Can't join with P thread");
}

