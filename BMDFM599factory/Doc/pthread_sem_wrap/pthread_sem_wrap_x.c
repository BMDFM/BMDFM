/******************************************************************************

pthread_sem_wrap_x.c - process-shared sema4 LD_PRELOAD wrapper

If the OS does not support pthread_mutex, pthread_rwlock, or pthread_cond,
they can be wrapped using semaphores as a fallback.

Howto
~~~~~
1. Build:
   cc -rdynamic -o BMDFMsrv BMDFMsrv.o cflp_udf.o -lm -lpthread
   cc -shared -fPIC -O3 -pthread -o libpthread_sem_wrap.so pthread_sem_wrap_x.c
2. Run:
   LD_PRELOAD=./libpthread_sem_wrap.so ./<BMDFM_app> <args>
3. Enjoy.

******************************************************************************/

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/mman.h>

#define PROC_SHARED 1

typedef int (*pthread_mutex_init_fn)(pthread_mutex_t*,
  const pthread_mutexattr_t*);
typedef int (*pthread_mutex_destroy_fn)(pthread_mutex_t*);
typedef int (*pthread_mutex_lock_fn)(pthread_mutex_t*);
typedef int (*pthread_mutex_trylock_fn)(pthread_mutex_t*);
typedef int (*pthread_mutex_timedlock_fn)(pthread_mutex_t*,
  const struct timespec*);
typedef int (*pthread_mutex_unlock_fn)(pthread_mutex_t*);
static pthread_mutex_init_fn real_pthread_mutex_init=NULL;
static pthread_mutex_destroy_fn real_pthread_mutex_destroy=NULL;
static pthread_mutex_lock_fn real_pthread_mutex_lock=NULL;
static pthread_mutex_trylock_fn real_pthread_mutex_trylock=NULL;
static pthread_mutex_timedlock_fn real_pthread_mutex_timedlock=NULL;
static pthread_mutex_unlock_fn real_pthread_mutex_unlock=NULL;

extern void shmempool_on(void);
extern void shmempool_off(void);
extern char is_shmempool_on(void);
extern void *reallocpool(void *ptr, unsigned long size);
extern void freepool(void *ptr);

/*****************************************************************************/
/***************      FALLBACK SECTION FOR BUILDS WITHOUT       **************/
/*************** getaddrpool(), getsizepool() and getbnkspool() **************/
/*****************************************************************************/
typedef unsigned long (*getaddrpool_fn)(void);
typedef unsigned long (*getsizepool_fn)(void);
typedef unsigned long (*getbnkspool_fn)(void);
static getaddrpool_fn real_getaddrpool=NULL;
static getsizepool_fn real_getsizepool=NULL;
static getbnkspool_fn real_getbnkspool=NULL;

struct shmempool_data_{
  unsigned long reserved0;
  unsigned long reserved1;
  union sem_{
    struct{
      int id;
      int index;
    } svr4sem4;
    pthread_mutex_t mutex;
  } sem0;
  union sem_ sem1;
  unsigned long reserved2;
  unsigned long shmempool_size;
  unsigned long shmempool_banks;
} *shmempool_data=NULL;

#define BM_DFM_CONNECTION_FILE_env "BM_DFM_CONNECTION_FILE_path"
#define BM_DFM_CONNECTION_FILE_default "/tmp/.BMDFMsrv"
#define SHMEM_MOUNT_ADDRESS_env "SHMEM_MOUNT_ADDRESS"
#ifndef MAP_FIXED_NOREPLACE
  #define MAP_FIXED_NOREPLACE (MAP_FIXED|MAP_EXCL)
#endif
static void get_shm_mnt_addr(void){
  char *BM_DFM_CONNECTION_FILE_path=NULL;
  FILE *f_descr;
  void *tst_map;
  if((BM_DFM_CONNECTION_FILE_path=getenv(BM_DFM_CONNECTION_FILE_env))==NULL)
    BM_DFM_CONNECTION_FILE_path=(char*)BM_DFM_CONNECTION_FILE_default;
  if((f_descr=fopen(BM_DFM_CONNECTION_FILE_path,"r"))!=NULL){
    if(fscanf(f_descr,"%*s %*d %lu",(unsigned long*)&shmempool_data)!=1)
      shmempool_data=NULL;
    fclose(f_descr);
  }
  if(!shmempool_data)
    if((BM_DFM_CONNECTION_FILE_path=getenv(SHMEM_MOUNT_ADDRESS_env))!=NULL)
      shmempool_data=(struct shmempool_data_*)atol(
        BM_DFM_CONNECTION_FILE_path);
  if(shmempool_data)
    if((tst_map=mmap((void*)shmempool_data,4096,PROT_NONE,
       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0))!=MAP_FAILED){
      if(tst_map==(void*)shmempool_data)
        shmempool_data=NULL;
      munmap(tst_map,4096);
    }
  return;
}

static unsigned long getaddrpool_stub(void){
  if(!shmempool_data)
    get_shm_mnt_addr();
  return (unsigned long)shmempool_data;
}

static unsigned long getsizepool_stub(void){
  if(!shmempool_data)
    get_shm_mnt_addr();
  return shmempool_data?shmempool_data->shmempool_size:0;
}

static unsigned long getbnkspool_stub(void){
  if(!shmempool_data)
    get_shm_mnt_addr();
  return shmempool_data?shmempool_data->shmempool_banks:0;
}

static void resolve_pool_fn(void){
  if(!real_getaddrpool)
    if(!(real_getaddrpool=(getaddrpool_fn)dlsym(RTLD_NEXT,"getaddrpool")))
      real_getaddrpool=&getaddrpool_stub;
  if(!real_getsizepool)
    if(!(real_getsizepool=(getsizepool_fn)dlsym(RTLD_NEXT,"getsizepool")))
      real_getsizepool=&getsizepool_stub;
  if(!real_getbnkspool)
    if(!(real_getbnkspool=(getbnkspool_fn)dlsym(RTLD_NEXT,"getbnkspool")))
      real_getbnkspool=&getbnkspool_stub;
  return;
}
/*****************************************************************************/
/************************ FALLBACK SECTION ENDS HERE. ************************/
/*****************************************************************************/

static unsigned long shm_own_sync_prims=0;
static pthread_mutex_t **shm_own_sync_prim_=NULL;
static sem_t **shm_own_sync_prim=NULL;

int pthread_mutex_init(pthread_mutex_t *mutex,
   const pthread_mutexattr_t *attr){
  char shm,buff[32];
  int pshared;
  if(!shmempool_data&&attr){
    pthread_mutexattr_getpshared(attr,&pshared);
    if(pshared==PTHREAD_PROCESS_SHARED){
      shmempool_data=(struct shmempool_data_*)((unsigned long)mutex-
        (unsigned long)&(((struct shmempool_data_*)0)->sem0.mutex));
      if((shmempool_data->reserved0==(unsigned long)sizeof(unsigned long))&&
         (shmempool_data->reserved1==(unsigned long)shmempool_data)){
        snprintf(buff,sizeof(buff),"%lu",(unsigned long)shmempool_data);
        setenv(SHMEM_MOUNT_ADDRESS_env,(const char*)buff,1);
      }
      else
        shmempool_data=NULL;
    }
  }
  resolve_pool_fn();
  if(((unsigned long)mutex<real_getaddrpool())||
     ((unsigned long)mutex>real_getaddrpool()+real_getsizepool())){
    if(!real_pthread_mutex_init)
      real_pthread_mutex_init=(pthread_mutex_init_fn)dlsym(RTLD_NEXT,
        "pthread_mutex_init");
    if(!real_pthread_mutex_init(mutex,NULL))
      return 0;
  }
  else{
    if(!shm_own_sync_prim){
      shm_own_sync_prims=real_getbnkspool()+2;
      shm_own_sync_prim_=(pthread_mutex_t**)malloc(sizeof(pthread_mutex_t*)*
        shm_own_sync_prims);
      shm_own_sync_prim=(sem_t**)malloc(sizeof(sem_t*)*shm_own_sync_prims);
      shm_own_sync_prims++;
    }
    if(shm_own_sync_prims){
      if(shm_own_sync_prims>1){
        shm_own_sync_prim_[shm_own_sync_prims-2]=mutex;
        if(!real_pthread_mutex_init)
          real_pthread_mutex_init=(pthread_mutex_init_fn)dlsym(RTLD_NEXT,
            "pthread_mutex_init");
        if(real_pthread_mutex_init(mutex,NULL))
          return errno;
      }
      shm_own_sync_prims--;
      if(!shm_own_sync_prims){
        shm_own_sync_prims=real_getbnkspool()+2;
        shm=is_shmempool_on();
        shmempool_on();
        while(shm_own_sync_prims){
          shm_own_sync_prim[shm_own_sync_prims-1]=(sem_t*)reallocpool(NULL,
            sizeof(sem_t));
          shm_own_sync_prims--;
          if(sem_init(shm_own_sync_prim[shm_own_sync_prims],PROC_SHARED,1))
            return errno;
        }
        if(!shm)
          shmempool_off();
        shm_own_sync_prims=real_getbnkspool()+2;
        if(!real_pthread_mutex_destroy)
          real_pthread_mutex_destroy=(pthread_mutex_destroy_fn)dlsym(RTLD_NEXT,
            "pthread_mutex_destroy");
        while(shm_own_sync_prims){
          shm_own_sync_prims--;
          if(real_pthread_mutex_destroy(
             shm_own_sync_prim_[shm_own_sync_prims]))
            return errno;
          *((sem_t**)shm_own_sync_prim_[shm_own_sync_prims])=
            shm_own_sync_prim[shm_own_sync_prims];
        }
        free((void*)shm_own_sync_prim_);
        shm_own_sync_prim_=NULL;
      }
      else
        return 0;
    }
    shm=is_shmempool_on();
    shmempool_on();
    *((sem_t**)mutex)=(sem_t*)reallocpool(NULL,sizeof(sem_t));
    if(!shm)
      shmempool_off();
    if(!sem_init(*((sem_t**)mutex),PROC_SHARED,1))
      return 0;
  }
  return errno;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex){
  char shm;
  unsigned long i,l;
  resolve_pool_fn();
  if(((unsigned long)mutex<real_getaddrpool())||
     ((unsigned long)mutex>real_getaddrpool()+real_getsizepool())||
     shm_own_sync_prims){
    if(!real_pthread_mutex_destroy)
      real_pthread_mutex_destroy=(pthread_mutex_destroy_fn)dlsym(RTLD_NEXT,
        "pthread_mutex_destroy");
    if(!real_pthread_mutex_destroy(mutex))
      return 0;
  }
  else
    if(!sem_destroy(*((sem_t**)mutex))){
      l=real_getbnkspool()+2;
      for(i=0;i<l;i++)
        if(*((sem_t**)mutex)==shm_own_sync_prim[i])
          break;
      if(i==l){
        shm=is_shmempool_on();
        shmempool_on();
        freepool(*((void**)mutex));
        *((unsigned long*)mutex)=0;
        if(!shm)
          shmempool_off();
      }
      return 0;
    }
  return errno;
}

int pthread_mutex_lock(pthread_mutex_t *mutex){
  resolve_pool_fn();
  if(((unsigned long)mutex<real_getaddrpool())||
     ((unsigned long)mutex>real_getaddrpool()+real_getsizepool())||
     shm_own_sync_prims){
    if(!real_pthread_mutex_lock)
      real_pthread_mutex_lock=(pthread_mutex_lock_fn)dlsym(RTLD_NEXT,
        "pthread_mutex_lock");
    if(!real_pthread_mutex_lock(mutex))
      return 0;
  }
  else
    if(!sem_wait(*((sem_t**)mutex)))
      return 0;
  return errno;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex){
  resolve_pool_fn();
  if(((unsigned long)mutex<real_getaddrpool())||
     ((unsigned long)mutex>real_getaddrpool()+real_getsizepool())||
     shm_own_sync_prims){
    if(!real_pthread_mutex_trylock)
      real_pthread_mutex_trylock=(pthread_mutex_trylock_fn)dlsym(RTLD_NEXT,
        "pthread_mutex_trylock");
    if(!real_pthread_mutex_trylock(mutex))
      return 0;
  }
  else
    if(!sem_trywait(*((sem_t**)mutex)))
      return 0;
  return errno;
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex,
   const struct timespec *abstime){
  resolve_pool_fn();
  if(((unsigned long)mutex<real_getaddrpool())||
     ((unsigned long)mutex>real_getaddrpool()+real_getsizepool())||
     shm_own_sync_prims){
    if(!real_pthread_mutex_timedlock)
      real_pthread_mutex_timedlock=(pthread_mutex_timedlock_fn)dlsym(RTLD_NEXT,
        "pthread_mutex_timedlock");
    if(!real_pthread_mutex_timedlock(mutex,abstime))
      return 0;
  }
  else
    if(!sem_timedwait(*((sem_t**)mutex),abstime))
      return 0;
  return errno;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex){
  resolve_pool_fn();
  if(((unsigned long)mutex<real_getaddrpool())||
     ((unsigned long)mutex>real_getaddrpool()+real_getsizepool())||
     shm_own_sync_prims){
    if(!real_pthread_mutex_unlock)
      real_pthread_mutex_unlock=(pthread_mutex_unlock_fn)dlsym(RTLD_NEXT,
        "pthread_mutex_unlock");
    if(!real_pthread_mutex_unlock(mutex))
      return 0;
  }
  else
    if(!sem_post(*((sem_t**)mutex)))
      return 0;
  return errno;
}

#include <sys/ipc.h>
#include <sys/sem.h>

struct svr4_sem4{
  int id;
  int index;
};

union semun_{
  int val;                    /* value for SETVAL */
  struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
  unsigned short int *array;  /* array for GETALL, SETALL */
  struct seminfo *__buf;      /* buffer for IPC_INFO */
};

struct cond_st_{
  long value;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
};

int pthread_cond_init(pthread_cond_t *cond,
   const pthread_condattr_t *attr){
  (void)attr;
  union semun_ semunion;
  if((((struct svr4_sem4*)cond)->id=semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|
     0x01B0))==-1)  /* 0x01B0 == 0660=="rw-rw----" */
    return errno;
  semunion.val=1;
  semctl(((struct svr4_sem4*)cond)->id,0,SETVAL,semunion);
  return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond){
  if(semctl(((struct svr4_sem4*)cond)->id,0,IPC_RMID)!=-1)
    return 0;
  return errno;
}

int pthread_cond_broadcast(pthread_cond_t *cond){
  union semun_ semunion;
  semunion.val=(int)*((long*)((char*)cond-(long)&(((struct cond_st_*)0)->
    cond)));
  semctl(((struct svr4_sem4*)cond)->id,0,SETVAL,semunion);
  return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex){
  (void)mutex;
  struct sembuf sem_cmd={0,0,0};  /* sem_num,sem_op,sem_flg */
  sem_cmd.sem_op=0;
  pthread_mutex_unlock((pthread_mutex_t*)((char*)cond-
    (long)&(((struct cond_st_*)0)->cond)+(long)&(((struct cond_st_*)0)->
    mutex)));
  while(semop(((struct svr4_sem4*)cond)->id,&sem_cmd,1)==-1);
  *((long*)((char*)cond-(long)&(((struct cond_st_*)0)->cond)))=0;
  return 0;
}


#ifdef REPLACE_RWLOCK_WITH_MUTEX

int pthread_rwlock_init(pthread_rwlock_t *rwlock,
   const pthread_rwlockattr_t *attr){
  (void)attr;
  pthread_mutexattr_t mattr;
  int rc;
  if((rc=pthread_mutexattr_init(&mattr)))
    return rc;
  if((rc=pthread_mutexattr_setpshared(&mattr,PROC_SHARED))){
    pthread_mutexattr_destroy(&mattr);
    return rc;
  }
  rc=pthread_mutex_init((pthread_mutex_t*)rwlock,&mattr);
  pthread_mutexattr_destroy(&mattr);
  return rc;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock){
  return pthread_mutex_destroy((pthread_mutex_t*)rwlock);
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock){
  return pthread_mutex_lock((pthread_mutex_t*)rwlock);
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock){
  return pthread_mutex_trylock((pthread_mutex_t*)rwlock);
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t *rwlock,
   const struct timespec *abstime){
  return pthread_mutex_timedlock((pthread_mutex_t*)rwlock,abstime);
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock){
  return pthread_mutex_lock((pthread_mutex_t*)rwlock);
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock){
  return pthread_mutex_trylock((pthread_mutex_t*)rwlock);
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *rwlock,
   const struct timespec *abstime){
  return pthread_mutex_timedlock((pthread_mutex_t*)rwlock,abstime);
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock){
  return pthread_mutex_unlock((pthread_mutex_t*)rwlock);
}

#else  /* REPLACE_RWLOCK_WITH_SEM */

int pthread_rwlock_init(pthread_rwlock_t *rwlock,
   const pthread_rwlockattr_t *attr){
  (void)attr;
  char shm;
  shm=is_shmempool_on();
  shmempool_on();
  *((sem_t**)rwlock)=(sem_t*)reallocpool(NULL,sizeof(sem_t));
  if(!shm)
    shmempool_off();
  if(!sem_init(*((sem_t**)rwlock),PROC_SHARED,1))
    return 0;
  return errno;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock){
  char shm;
  if(!sem_destroy(*((sem_t**)rwlock))){
    shm=is_shmempool_on();
    shmempool_on();
    freepool(*((void**)rwlock));
    *((unsigned long*)rwlock)=0;
    if(!shm)
      shmempool_off();
    return 0;
  }
  return errno;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock){
  if(!sem_wait(*((sem_t**)rwlock)))
    return 0;
  return errno;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock){
  if(!sem_trywait(*((sem_t**)rwlock)))
    return 0;
  return errno;
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t *rwlock,
   const struct timespec *abstime){
  if(!sem_timedwait(*((sem_t**)rwlock),abstime))
    return 0;
  return errno;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock){
  if(!sem_wait(*((sem_t**)rwlock)))
    return 0;
  return errno;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock){
  if(!sem_trywait(*((sem_t**)rwlock)))
    return 0;
  return errno;
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *rwlock,
   const struct timespec *abstime){
  if(!sem_timedwait(*((sem_t**)rwlock),abstime))
    return 0;
  return errno;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock){
  if(!sem_post(*((sem_t**)rwlock)))
    return 0;
  return errno;
}

#endif  /* REPLACE_RWLOCK_WITH_MUTEX */

