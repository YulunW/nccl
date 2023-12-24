// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#ifndef NCCL_CVARS_H_INCLUDED
#define NCCL_CVARS_H_INCLUDED

#include <string>
#include <vector>

// Automatically generated by ./maint/extractcvars.py --- START
// DO NOT EDIT!!!
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

extern uint64_t NCCL_DDA2_ALLREDUCE_TREE_THRESHOLD_NVS;

extern uint64_t NCCL_DDA2_TMPBUFF_SIZE;

extern bool NCCL_DDA_ALLREDUCE_LARGE_MESSAGE_HCM;

extern int NCCL_DDA_ALLREDUCE_MAX_BLOCKS;

extern uint64_t NCCL_DDA_ALLREDUCE_TMPBUFF_SIZE;

extern uint64_t NCCL_DDA_ALLREDUCE_TREE_THRESHOLD_HCM;

extern uint64_t NCCL_DDA_ALLREDUCE_TREE_THRESHOLD_NVS;

extern bool NCCL_DDA_FORCE_P2P_ACCESS;

extern int NCCL_DDA_MAX_RANKS;

extern std::string NCCL_IB_HCA_PREFIX;
extern std::vector<std::string> NCCL_IB_HCA;

enum class NCCL_SENDRECV_ALGO {
  orig,
  ctran,
};
extern enum NCCL_SENDRECV_ALGO NCCL_SENDRECV_ALGO;

// Automatically generated by ./maint/extractcvars.py --- END


void ncclCvarInit();

#endif  /* NCCL_CVARS_H_INCLUDED */
