// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

// Automatically generated by ./maint/extractcvars.py --- START
// DO NOT EDIT!!!

#ifndef NCCL_CVARS_H_INCLUDED
#define NCCL_CVARS_H_INCLUDED

#include <string>
#include <vector>

enum class NCCL_ALLGATHER_ALGO {
  orig,
  ctdirect,
  ctring,
  ctrd,
};
extern enum NCCL_ALLGATHER_ALGO NCCL_ALLGATHER_ALGO;

extern uint64_t NCCL_ALLGATHER_DIRECT_CUTOFF;

enum class NCCL_ALLREDUCE_ALGO {
  orig,
  dda,
};
extern enum NCCL_ALLREDUCE_ALGO NCCL_ALLREDUCE_ALGO;

enum class NCCL_ALLREDUCE_ALGO2 {
  orig,
  dda,
};
extern enum NCCL_ALLREDUCE_ALGO2 NCCL_ALLREDUCE_ALGO2;

extern int NCCL_ALLREDUCE_SPARSE_BLOCK_NUM_THREAD_BLOCKS;

extern int NCCL_ALLREDUCE_SPARSE_BLOCK_THREAD_BLOCK_SIZE;

extern int64_t NCCL_CROSS_NIC;

enum class NCCL_CTRAN_BACKENDS {
  ib,
};
extern std::vector<enum NCCL_CTRAN_BACKENDS> NCCL_CTRAN_BACKENDS;

extern int NCCL_CTRAN_IB_MAX_QPS;

extern uint64_t NCCL_CTRAN_IB_QP_SCALING_THRESHOLD;

extern bool NCCL_CTRAN_IB_TRAFFIC_PROFILNG;

extern std::string NCCL_CTRAN_KINETO_PROFILE_DIR;

enum class NCCL_CTRAN_PROFILING {
  none,
  stdout,
  info,
  kineto,
};
extern enum NCCL_CTRAN_PROFILING NCCL_CTRAN_PROFILING;

extern int NCCL_CTRAN_PROFILING_REPORT_COUNT;

enum class NCCL_CTRAN_REGISTER {
  none,
  lazy,
  eager,
};
extern enum NCCL_CTRAN_REGISTER NCCL_CTRAN_REGISTER;

extern int NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT;

extern std::string NCCL_CTRAN_TOPO_FILE;

extern std::vector<std::string> NCCL_CTRAN_TOPO_FILE_KEYS;

extern int NCCL_DDA2_ALLREDUCE_MAX_BLOCKS;

extern uint64_t NCCL_DDA2_ALLREDUCE_SCATGAT_THRESHOLD;

extern uint64_t NCCL_DDA2_ALLREDUCE_TREE_THRESHOLD;

extern uint64_t NCCL_DDA2_TMPBUFF_SIZE;

extern bool NCCL_DDA_ALLREDUCE_LARGE_MESSAGE_HCM;

extern int NCCL_DDA_ALLREDUCE_MAX_BLOCKS;

extern uint64_t NCCL_DDA_ALLREDUCE_TMPBUFF_SIZE;

extern uint64_t NCCL_DDA_ALLREDUCE_TREE_THRESHOLD_HCM;

extern uint64_t NCCL_DDA_ALLREDUCE_TREE_THRESHOLD_NVS;

extern bool NCCL_DDA_FORCE_P2P_ACCESS;

extern int NCCL_DDA_MAX_RANKS;

extern int64_t NCCL_GDR_FLUSH_DISABLE;

extern int64_t NCCL_IB_ADAPTIVE_ROUTING;

extern int64_t NCCL_IB_AR_THRESHOLD;

extern int64_t NCCL_IB_DISABLE;

extern int64_t NCCL_IB_GID_INDEX;

extern std::string NCCL_IB_HCA_PREFIX;
extern std::vector<std::string> NCCL_IB_HCA;

extern int64_t NCCL_IB_MERGE_VFS;

extern int64_t NCCL_IB_PCI_RELAXED_ORDERING;

extern int64_t NCCL_IB_PKEY;

extern int64_t NCCL_IB_QPS_PER_CONNECTION;

extern int64_t NCCL_IB_RETRY_CNT;

extern int64_t NCCL_IB_SL;

extern int64_t NCCL_IB_SPLIT_DATA_ON_QPS;

extern int64_t NCCL_IB_TC;

extern int64_t NCCL_IB_TIMEOUT;

extern int64_t NCCL_IB_USE_INLINE;

extern int64_t NCCL_IGNORE_CPU_AFFINITY;

extern int64_t NCCL_IGNORE_DISABLED_P2P;

extern int64_t NCCL_MAX_NCHANNELS;

extern int64_t NCCL_MAX_NRINGS;

extern int64_t NCCL_MAX_P2P_NCHANNELS;

extern int64_t NCCL_MIN_NCHANNELS;

extern int64_t NCCL_MIN_NRINGS;

extern int64_t NCCL_MIN_P2P_NCHANNELS;

extern int64_t NCCL_NCHANNELS_PER_NET_PEER;

extern int64_t NCCL_NET_DISABLE_INTRA;

extern int64_t NCCL_NET_FORCE_FLUSH;

extern int64_t NCCL_NET_GDR_READ;

extern int64_t NCCL_NVB_DISABLE;

extern int64_t NCCL_P2P_PXN_LEVEL;

extern int64_t NCCL_PXN_DISABLE;

enum class NCCL_SENDRECV_ALGO {
  orig,
  ctran,
};
extern enum NCCL_SENDRECV_ALGO NCCL_SENDRECV_ALGO;

extern int64_t NCCL_TOPO_DUMP_FILE_RANK;


void ncclCvarInit();

#endif  /* NCCL_CVARS_H_INCLUDED */
// Automatically generated by ./maint/extractcvars.py --- END
