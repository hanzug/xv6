#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1; // 需要的线程数
static int round = 0;
static __thread int thread_flag = 0;  //c++ 11 新特性，相当于每个线程各自的全局变量，互不影响


struct barrier{
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread; // 当前轮已经有多少线程
  int round; // 轮数
  int flag;
}bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  // assert 函数返回值应当如此，否则返回错误。
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);

  bstate.round = 0;
  bstate.nthread = 0;
  bstate.flag = 0;
}

static
barrier(){
  pthread_mutex_lock(&bstate.barrier_mutex);
  thread_flag = !thread_flag;

// 等待上一轮最后一个线程翻页，0/1表示当前轮和上一轮。
  while (thread_flag == bstate.flag){
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }

  int arrived = ++bstate.nthread;
  if (arrived == nthread) {
    bstate.round ++;
    bstate.flag = !bstate.flag;
    bstate.nthread = 0;
    pthread_cond_broadcast(&bstate.barrier_cond);
  } else {
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
}

static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}
