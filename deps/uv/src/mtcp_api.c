/* #define _GNU_SOURCE */
#include <sys/types.h>
#include <rte_ethdev.h>
#include <mtcp_api.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>

extern int mtcp_init_wrapper();
extern int mtcp_getconf();
extern struct mtcp_context *g_mctx[];

#define MAX_CPUS             4 /* RTE_MAX_LCORE */

#define MBUF_SIZE            (2048 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define NB_MBUF              8192
#define MEMPOOL_CACHE_SIZE   256

#define RX_PTHRESH      8 /**< Default values of RX prefetch threshold reg. */
#define RX_HTHRESH      8 /**< Default values of RX host threshold reg. */
#define RX_WTHRESH      4 /**< Default values of RX write-back threshold reg. */

#define TX_PTHRESH      32 /**< Default values of TX prefetch threshold reg. */
#define TX_HTHRESH      0  /**< Default values of TX host threshold reg. */
#define TX_WTHRESH      0  /**< Default values of TX write-back threshold reg. */

#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 128

static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

/* packet memory pool for storing packet bufs */
struct rte_mempool *pktmbuf_pool[MAX_CPUS] = {NULL};

static struct rte_eth_conf port_conf = {
  .rxmode = {
    .mq_mode  =   ETH_MQ_RX_RSS,
    .max_rx_pkt_len =   ETHER_MAX_LEN,
    .split_hdr_size =   0,
    .header_split   =   0, /**< Header Split disabled */
    .hw_ip_checksum =   1, /**< IP checksum offload enabled */
    .hw_vlan_filter =   0, /**< VLAN filtering disabled */
    .jumbo_frame    =   0, /**< Jumbo Frame Support disabled */
    .hw_strip_crc   =   1, /**< CRC stripped by hardware */
  },
  .rx_adv_conf = {
    .rss_conf = {
      .rss_key =  NULL,
      .rss_hf =   ETH_RSS_TCP
    },
  },
  .txmode = {
    .mq_mode =    ETH_MQ_TX_NONE,
  },
  .fdir_conf = {
    .mode = RTE_FDIR_MODE_PERFECT,
    .pballoc = RTE_FDIR_PBALLOC_256K,
    .status = RTE_FDIR_REPORT_STATUS_ALWAYS,
    //.flexbytes_offset = 0x6,
    .drop_queue = 127,
  },
};

static const struct rte_eth_rxconf rx_conf = {
  .rx_thresh = {
    .pthresh =    RX_PTHRESH, /* RX prefetch threshold reg */
    .hthresh =    RX_HTHRESH, /* RX host threshold reg */
    .wthresh =    RX_WTHRESH, /* RX write-back threshold reg */
  },
  .rx_free_thresh =     32,
};

static const struct rte_eth_txconf tx_conf = {
  .tx_thresh = {
    .pthresh =    TX_PTHRESH, /* TX prefetch threshold reg */
    .hthresh =    TX_HTHRESH, /* TX host threshold reg */
    .wthresh =    TX_WTHRESH, /* TX write-back threshold reg */
  },
  .tx_free_thresh =     0, /* Use PMD default values */
  .tx_rs_thresh =     0, /* Use PMD default values */
  .txq_flags =      0x0,
};

#define MAX_DEVICES 16
struct mtcp_ctx_dev_prm dev_prm[MAX_DEVICES];
int mtcp_context_coreid=0;

int node_mtcp_init_port() 
{
  struct mtcp_conf conf;
  struct mtcp_ctx_dev_prm dprm[MAX_DEVICES];
  mctx_t mctx;

  /* setting the rss key */
  static const uint8_t key[] = {
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05
  };

  port_conf.rx_adv_conf.rss_conf.rss_key = (uint8_t *)&key;
  port_conf.rx_adv_conf.rss_conf.rss_key_len = sizeof(key);

  int cpu = sched_getcpu();
  if (cpu < 0) {
    return -1;
  }

  pthread_t thread;
  thread = pthread_self();
  cpu_set_t cpuset; 
  CPU_ZERO(&cpuset); 
  int j=0;
  for(j=0; j < 4; j++) {
    CPU_SET(j, &cpuset); 
  }
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s < 0 ){
    printf("Incorrect affinity\n");
  }

  int portid=0, rxlcore_id, ret;
  mtcp_getconf(&conf);

  /* Allocate memory for receive and transmit buffer */
  char name[20];

  /* create the mbuf pools */
  rxlcore_id = cpu;
  for (rxlcore_id = 0; rxlcore_id < 4; rxlcore_id++) {
    sprintf(name, "mbuf_pool-%d", rxlcore_id);
    printf("-----> Creating up mbuf_pool: [%s]\n", name);
    fflush(stdout);
    pktmbuf_pool[rxlcore_id] = 
      rte_mempool_create(name, NB_MBUF,
                     MBUF_SIZE, MEMPOOL_CACHE_SIZE,
                     sizeof(struct rte_pktmbuf_pool_private),
                     rte_pktmbuf_pool_init, NULL,
                     rte_pktmbuf_init, NULL,
                     rte_socket_id(), 0);
    if (pktmbuf_pool[rxlcore_id] == NULL) {
      rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");
    }
    printf("-----> Created rte_membuf pool:[%s]\n", name);
    fflush(stdout);
  }

  //Initialize port
  mtcp_getconf(&conf);
  printf("Initializing port %u...", (unsigned) portid);
  fflush(stdout);

  printf("Configuring device with portid:[%d]\n", portid);
  ret = rte_eth_dev_configure(portid, 1, 1, &port_conf);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
             ret, (unsigned) portid);
    fflush(stdout);
  }
  printf("Configuring device with portid:[%d] is done\n", portid);
  fflush(stdout);

  /* init one RX queue per CORE */
  int number_of_queues = 1;
    
  printf("Setting up rx and tx queues\n");
  fflush(stdout);
  for (rxlcore_id = 0; rxlcore_id < number_of_queues; rxlcore_id++) {
    ret = rte_eth_rx_queue_setup(portid, rxlcore_id, nb_rxd,
                                rte_eth_dev_socket_id(portid), &rx_conf, 
                                 pktmbuf_pool[rxlcore_id]);
    if (ret < 0) {
      rte_exit(EXIT_FAILURE,
             "rte_eth_rx_queue_setup:err=%d, port=%u, queueid: %d\n",
              ret, (unsigned) portid, rxlcore_id);
    }
  }
  printf("Setting up rx queue is success\n");
  fflush(stdout);

  /* init one TX queue per CORE */
  printf("Setting up tx queue\n");
  fflush(stdout);
  for (rxlcore_id = 0; rxlcore_id < number_of_queues; rxlcore_id++) {    
    ret = rte_eth_tx_queue_setup(portid, rxlcore_id, nb_txd,
                               rte_eth_dev_socket_id(portid), &tx_conf);
    if (ret < 0) {
       rte_exit(EXIT_FAILURE,
             "rte_eth_tx_queue_setup:err=%d, port=%u, queueid: %d\n",
             ret, (unsigned) portid, rxlcore_id);
    }
  }
  printf("Setting up tx queue is success\n");
  fflush(stdout);

  /* Start device */
  printf("Setting up devices\n");
  fflush(stdout);
  ret = rte_eth_dev_start(portid);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
             ret, (unsigned) portid);
  }
  printf("Setting up devices is success\n");
  fflush(stdout);
  rte_eth_promiscuous_enable(portid);
  dev_prm[portid].port = portid;

  /* This is main thread */

  if (setting_up_mtcp_context() < 0) {
    printf("Couldn't create mtcp context\n");
    return -1;
  }
   
  printf("mtcp_init_port is a success\n");
  return 0;
}

/* setting_up_mtcp_context convenience function */
int setting_up_mtcp_context() {
  printf("In setting_up_mtcp_context\n");
  rte_cpuset_t cpuset;

  int cpu, s;
  cpu = sched_getcpu();
  if (cpu < 0)
    return -1;

  s = pthread_getaffinity_np(pthread_self(), sizeof(rte_cpuset_t),
                             (cpu_set_t *)&cpuset);
  if (s != 0)
    return -1;

  rte_thread_set_affinity(&cpuset);

  mctx_t mctx;
  mctx = mtcp_get_current_mctx();
  if (mctx != NULL) {
    printf("mTCP context exists for core[%d]\n", cpu);
    return 0;
  }

  struct mtcp_ctx_dev_prm dprm[MAX_DEVICES];
  struct mtcp_ctx_prm prm;
  struct mtcp_conf conf;
  int portid=0;
  
  mtcp_getconf(&conf);

  /* mempool attach */
  int rxlcore_id = cpu;
  {
    char name[20];
    sprintf(name, "mbuf_pool-%d", rxlcore_id);
    printf("using mbuf_pool is [%s]\n", name);
    /* initialize the mbuf pools */
    pktmbuf_pool[rxlcore_id] = rte_mempool_lookup(name);
    if (pktmbuf_pool[rxlcore_id] == NULL)
      rte_exit(EXIT_FAILURE, "Cannot attach mbuf pool\n");
  }

  prm.cpu = rxlcore_id;
  prm.mpool = pktmbuf_pool[rxlcore_id];
  prm.dev_num = 0;

  unsigned sock_id = (unsigned)rte_eth_dev_socket_id(portid);
  if ((unsigned)rte_socket_id() == sock_id || sock_id == (unsigned)-1) {
    memcpy(&dprm[prm.dev_num], &dev_prm[portid], sizeof(dev_prm[portid]));
    dprm[prm.dev_num].queue = cpu % conf.num_cores;
    dprm[prm.dev_num].rx_offload = DEV_RX_OFFLOAD_IPV4_CKSUM;
    dprm[prm.dev_num].tx_offload =
      DEV_TX_OFFLOAD_TCP_TSO |
      DEV_TX_OFFLOAD_IPV4_CKSUM |
      DEV_TX_OFFLOAD_TCP_CKSUM;

    prm.dev_num++;
  }

  prm.dev_prm = dprm;

  if (prm.dev_num == 0) {
    printf("No associate port\n" );
    return;
  }

  mtcp_set_current_cpu(rxlcore_id);
  mctx = mtcp_create_context(&prm);
  if (mctx == NULL) {
    printf("Couldn't create mtcp context on core:[%d]\n", rxlcore_id);
    return -1;
  }
  mtcp_context_coreid = rxlcore_id;
  mtcp_set_current_mctx(mctx);
  printf("mctx is[%x] on core[%d]\n", g_mctx[rxlcore_id], rxlcore_id);
  printf("Out of setting_up_mtcp_context\n");
  return 0; 
}

int node_mtcp_init_wrapper() 
{
  print_thread_info("node_mtcp_init_wrapper");
  if (mtcp_init_wrapper() < 0 ) {
    printf("mtcp_init_wrapper() failed\n");
    exit(-1);
  }
  printf("mtcp_init_wrapper() success\n");

  if (node_mtcp_init_port() < 0) {
    printf("node_mtcp_init_port() failed\n");
    exit(-1);
  }
  printf("node_mtcp_init_port() success\n");

  return 0;
}

void print_thread_info(char *fname) {
/*
  pthread_t thread;
  thread = pthread_self();
  int i=0;
  printf("\nfname:[%s], Thread information: [", fname);
  unsigned char *ptc = (unsigned char *)(void *)(&thread);
  for(i=0; i < sizeof(thread); i++) {
    printf("%02x", (unsigned)ptc[i]);
  }
  printf("]\n");
  fflush(stdout);
  pid_t tid;
  tid = syscall(SYS_gettid);
  printf("print_thread_info(): [%s] tid: %ld\n", fname, tid); fflush(stdout);
*/
}
