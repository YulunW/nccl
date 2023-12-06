// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "CtranMapper.h"
#include <unistd.h>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include "CtranMapperImpl.h"
#include "comm.h"
#include "nccl_cvars.h"

/*
=== BEGIN_NCCL_CVAR_INFO_BLOCK ===

 - name        : NCCL_CTRAN_PROFILING
   type        : enum
   default     : none
   choices     : none, stdout, info, kineto
   description : |-
     Kind of ctran profiling needed.
     none - No profiling
     stdout - Dump profiling data to stdout
     info   - Dump profiling data to NCCL_DEBUG INFO
     kineto - Dump profiling data to a kineto log
        (for kineto profiling, see also NCCL_CTRAN_KINETO_PROFILE_DIR)

 - name        : NCCL_CTRAN_KINETO_PROFILE_DIR
   type        : string
   default     : "/tmp"
   description : |-
     Directory to place Ctran kineto profiling logs.
     (see also NCCL_CTRAN_PROFILING)

 - name        : NCCL_CTRAN_REGISTER
   type        : enum
   default     : lazy
   choices     : none, lazy, eager
   description : |-
     Kind of registration to use for ctran user buffers
     none - No registration
     lazy - Lazy registration (keep track of user-provided registration
            buffers, but delay the actual registration till the buffer
            is used for a communication operation)
     eager - Eager registration (register buffers as soon as it is
             provided by the user)

 - name        : NCCL_CTRAN_BACKENDS
   type        : enumlist
   default     : ib
   choices     : ib
   description : |-
     Backends to enable for ctran
     ib - RoCE/IB backend

 - name        : NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT
   type        : int
   default     : -1
   description : |-
     Manages the frequency of register snapshot reporting. Set to -1 to
     completely disable. Set to 0 to report only at communicator destroy time. Set to
     N to allows a snapshot to be reported whenever once every N registrations. It
     helps understand the performance impact of registeration at different period of
     a long running job.

 - name        : NCCL_CTRAN_PROFILING_REPORT_COUNT
   type        : int
   default     : 100
   description : |-
     Number of ops to report CTRAN profiling results periodically

=== END_NCCL_CVAR_INFO_BLOCK ===
*/

enum GlobalRegistDurationType { REG_MEM, DEREG_MEM, LOOKUP_HIT, LOOKUP_MISS };

static std::unordered_map<GlobalRegistDurationType, std::string>
    globalRegistDurationTypeNameMap = {
        {REG_MEM, "registration"},
        {DEREG_MEM, "deregistration"},
        {LOOKUP_HIT, "lookup-hit"},
        {LOOKUP_MISS, "lookup-miss"},
};
static std::unordered_map<uint64_t, CtranMapper*> allCommHashCtranMapperMap;
static std::unordered_map<GlobalRegistDurationType, std::vector<double>>
    allCommRegistDurationsMap;
static std::mutex allCommMutex;

static double sumDurations(std::vector<double>& durs) {
  double total = 0;
  for (auto& dur : durs) {
    total += dur;
  }
  return total;
}

static void reportGlobalRegSnapshot(void) {
  const std::lock_guard<std::mutex> lock(allCommMutex);

  // Counts per communicator
  for (auto& it : allCommHashCtranMapperMap) {
    auto& mapper = it.second;
    mapper->reportRegSnapshot();
  }

  // Timers accumulated from all communicators
  for (auto& it : allCommRegistDurationsMap) {
    auto& key = it.first;
    auto& durs = it.second;
    size_t numDurs = durs.size();
    if (numDurs) {
      double totalLat = sumDurations(durs);
      INFO(
          NCCL_INIT,
          "CTRAN-MAPPER: [register snapshot] total %s latency across all comms %.2f ms, average %.2f ms across %lu %s",
          globalRegistDurationTypeNameMap[key].c_str(),
          totalLat,
          totalLat / numDurs,
          numDurs,
          globalRegistDurationTypeNameMap[key].c_str());
    }
  }
}

static void recordRegistDuration(
    GlobalRegistDurationType key,
    double duration) {
  allCommMutex.lock();
  allCommRegistDurationsMap[key].push_back(duration);

  // Allow periodical snapshot report during long job running
  bool shouldReport = false;
  if (key == GlobalRegistDurationType::REG_MEM &&
      NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT > 0 &&
      (allCommRegistDurationsMap[key].size() %
           NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT ==
       0)) {
    shouldReport = true;
  }
  allCommMutex.unlock();

  // Call report after unlock since we will lock again inside
  // reportGlobalRegSnapshot
  if (shouldReport) {
    reportGlobalRegSnapshot();
  }
}

CtranMapper::CtranMapper(ncclComm* comm) {
  this->pimpl_ = std::unique_ptr<impl>(new impl());

  /* mapperRegElemList */
  this->pimpl_->mapperRegElemList =
      std::unique_ptr<class CtranAvlTree>(new class CtranAvlTree());

  /* check user preference for backends */
  for (auto b : NCCL_CTRAN_BACKENDS) {
    if (b == NCCL_CTRAN_BACKENDS::ib) {
      this->pimpl_->backends.push_back(CtranMapperBackend::IB);
    }
  }

  /* enable available backends
   * NOTE: currently only support IB backend
   */
  this->pimpl_->ctranIb = nullptr;
  auto it = std::find(
      this->pimpl_->backends.begin(),
      this->pimpl_->backends.end(),
      CtranMapperBackend::IB);
  /* initialize Ctran IB backend */
  if (it != this->pimpl_->backends.end()) {
    try {
      this->pimpl_->ctranIb =
          std::unique_ptr<class CtranIb>(new class CtranIb(comm));
    } catch (const std::bad_alloc& e) {
      WARN("CTRAN: IB backend not enabled");
    }
  }

  /* create rankBackendMap, index 'i' indicates the backend used for rank 'i' */
  for (int i = 0; i < comm->nRanks; i++) {
    if (this->pimpl_->ctranIb != nullptr) {
      this->pimpl_->rankBackendMap.push_back(CtranMapperBackend::IB);
    } else {
      this->pimpl_->rankBackendMap.push_back(CtranMapperBackend::UNSET);
    }
  }

  this->pimpl_->numRegistrations = 0;
  this->pimpl_->numCachedRegistrations = 0;
  this->pimpl_->totalNumDynamicRegistrations = 0;
  this->pimpl_->totalNumRegistrations = 0;
  this->pimpl_->totalNumCachedRegistrations = 0;
  this->pimpl_->totalNumRegLookupHit = 0;
  this->pimpl_->totalNumRegLookupMiss = 0;

  CUDACHECKIGNORE(
      cudaStreamCreateWithFlags(&this->internalStream, cudaStreamNonBlocking));

  this->rank = comm->rank;
  this->commHash = comm->commHash;

  if (NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT >= 0) {
    allCommMutex.lock();
    allCommHashCtranMapperMap[this->commHash] = this;
    allCommMutex.unlock();
  }
}

void CtranMapper::reportRegSnapshot(void) {
  INFO(
      NCCL_INIT,
      "CTRAN-MAPPER: [register snapshot] buffer registration with commHash %lu: "
      "total cached %u total registered %u total dynamically registered %u, total lookup hits %u misses %u",
      this->commHash,
      this->pimpl_->totalNumCachedRegistrations,
      this->pimpl_->totalNumRegistrations,
      this->pimpl_->totalNumDynamicRegistrations,
      this->pimpl_->totalNumRegLookupHit,
      this->pimpl_->totalNumRegLookupMiss);
}

void CtranMapper::reportProfiling(bool flush) {
  /* flush timestamps */
  if (!this->timestamps.empty() &&
      ((this->timestamps.size() > NCCL_CTRAN_PROFILING_REPORT_COUNT ||
        flush))) {
    if (NCCL_CTRAN_PROFILING == NCCL_CTRAN_PROFILING::stdout ||
        NCCL_CTRAN_PROFILING == NCCL_CTRAN_PROFILING::info) {
      std::stringstream ss;
      ss << "[CTRAN-MAPPER] Communication Profiling:" << std::endl;
      for (auto& ts : this->timestamps) {
        ss << "    collective=" << ts->algo << std::endl;
        ss << "    startTime="
           << std::chrono::duration_cast<std::chrono::nanoseconds>(
                  ts->start.time_since_epoch())
                  .count()
           << std::endl;
        for (auto& tsp : ts->recvCtrl) {
          ss << "        recvCtrl[" << tsp.peer << "]="
             << std::chrono::duration_cast<std::chrono::nanoseconds>(
                    tsp.now.time_since_epoch())
                    .count()
             << std::endl;
        }
        for (auto& tsp : ts->putIssued) {
          ss << "        putIssued[" << tsp.peer << "]="
             << std::chrono::duration_cast<std::chrono::nanoseconds>(
                    tsp.now.time_since_epoch())
                    .count()
             << std::endl;
        }
        for (auto& tsp : ts->putComplete) {
          ss << "        putComplete[" << tsp.peer << "]="
             << std::chrono::duration_cast<std::chrono::nanoseconds>(
                    tsp.now.time_since_epoch())
                    .count()
             << std::endl;
        }
        if (NCCL_CTRAN_PROFILING == NCCL_CTRAN_PROFILING::info) {
          INFO(NCCL_INIT, "%s", ss.str().c_str());
          ss.str("");
          ss.clear();
        }
      }
      if (NCCL_CTRAN_PROFILING == NCCL_CTRAN_PROFILING::stdout) {
        std::cout << ss.str() << std::flush;
      }
    } else if (NCCL_CTRAN_PROFILING == NCCL_CTRAN_PROFILING::kineto) {
      auto pid = getpid();
      static uint64_t reportCnt = 0;
      std::string filename(
          NCCL_CTRAN_KINETO_PROFILE_DIR + std::string("/nccl_ctran_log.") +
          std::to_string(pid) + std::string(".rank") +
          std::to_string(this->rank) + std::string(".comm") +
          std::to_string(this->commHash) + std::string(".") +
          std::to_string(reportCnt++) + std::string(".json"));
      INFO(NCCL_ALL, "Dumping ctran profile to %s\n", filename.c_str());
      std::ofstream f(filename);
      int id = 0;
      f << "[" << std::endl;
      for (auto& ts : this->timestamps) {
        int collId = id;
        f << "{\"name\": \"" << ts->algo << "\", "
          << "\"cat\": \"COL\", "
          << "\"id\": \"" << id++ << "\", "
          << "\"ph\": \"b\", "
          << "\"pid\": \"0\", "
          << "\"ts\": \""
          << std::chrono::duration_cast<std::chrono::milliseconds>(
                 ts->start.time_since_epoch())
                 .count()
          << "\"}," << std::endl;
        CtranMapperTimestampPoint last(0);
        for (auto& tsp : ts->recvCtrl) {
          f << "{\"name\": \"recvCtrl\", "
            << "\"cat\": \"NET\", "
            << "\"id\": \"" << id++ << "\", "
            << "\"ph\": \"X\", "
            << "\"pid\": \"" << tsp.peer << "\", "
            << "\"ts\": \""
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   tsp.now.time_since_epoch())
                   .count()
            << "\", \"dur\": \"0\""
            << "}," << std::endl;
        }
        for (auto& tsp : ts->putIssued) {
          f << "{\"name\": \"put\", "
            << "\"cat\": \"NET\", "
            << "\"id\": \"" << id++ << "\", "
            << "\"ph\": \"b\", "
            << "\"pid\": \"" << tsp.peer << "\", "
            << "\"ts\": \""
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   tsp.now.time_since_epoch())
                   .count()
            << "\"}," << std::endl;
        }
        id -= ts->putIssued.size();
        for (auto& tsp : ts->putComplete) {
          f << "{\"name\": \"put\", "
            << "\"cat\": \"NET\", "
            << "\"id\": \"" << id++ << "\", "
            << "\"ph\": \"e\", "
            << "\"pid\": \"" << tsp.peer << "\", "
            << "\"ts\": \""
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   tsp.now.time_since_epoch())
                   .count()
            << "\"}," << std::endl;
          last = tsp;
        }
        f << "{\"name\": \"" << ts->algo << "\", "
          << "\"cat\": \"COL\", "
          << "\"id\": \"" << collId << "\", "
          << "\"ph\": \"e\", "
          << "\"pid\": \"0\", "
          << "\"ts\": \""
          << std::chrono::duration_cast<std::chrono::milliseconds>(
                 last.now.time_since_epoch())
                 .count()
          << "\"}," << std::endl;
      }
      f << "]" << std::endl;
      f.close();
      f.flush();
    }
    this->timestamps.clear();
  }
}

CtranMapper::~CtranMapper() {
  this->reportProfiling(true);

  /* safely de-register any bufferes applications may miss */
  auto v = this->pimpl_->mapperRegElemList->getAllElems();
  for (auto hdl : v) {
    NCCLCHECKIGNORE(this->deregMem(hdl));
  }

  if (NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT >= 0) {
    // Report summary of this communicator before destroying it
    this->reportRegSnapshot();

    bool lastMapper = false;
    allCommMutex.lock();
    allCommHashCtranMapperMap.erase(this->commHash);
    lastMapper = allCommHashCtranMapperMap.empty();
    allCommMutex.unlock();

    // Report global counters after all communicators have been destroyed
    // Call report after unlock since we will lock again inside
    // reportGlobalRegSnapshot
    if (lastMapper) {
      reportGlobalRegSnapshot();
    }
  }

  CUDACHECKIGNORE(cudaStreamDestroy(this->internalStream));
}

ncclResult_t CtranMapper::impl::regMem(
    struct CtranMapperRegElem* mapperRegElem) {
  ncclResult_t res = ncclSuccess;
  auto dur = CtranMapperTimer();

  if (this->ctranIb != nullptr) {
    assert(mapperRegElem->ibRegElem == nullptr);
    NCCLCHECKGOTO(
        this->ctranIb->regMem(
            mapperRegElem->buf, mapperRegElem->len, &mapperRegElem->ibRegElem),
        res,
        exit);
  }

  mapperRegElem->state = CtranMapperRegElemState::REGISTERED;
  if (NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT >= 0) {
    this->numRegistrations++;
    this->totalNumRegistrations++;
    recordRegistDuration(GlobalRegistDurationType::REG_MEM, dur.durationMs());
  }

  INFO(
      NCCL_COLL,
      "CTRAN-MAPPER: registered buffer %p len %ld, state %d",
      mapperRegElem->buf,
      mapperRegElem->len,
      mapperRegElem->state);

exit:
  return res;
}

ncclResult_t CtranMapper::impl::deregMem(
    struct CtranMapperRegElem* mapperRegElem) {
  ncclResult_t res = ncclSuccess;
  auto dur = CtranMapperTimer();

  if (this->ctranIb != nullptr) {
    NCCLCHECKGOTO(this->ctranIb->deregMem(mapperRegElem->ibRegElem), res, exit);
  }

  INFO(
      NCCL_COLL,
      "CTRAN-MAPPER: deregister buffer %p len %ld, state %d",
      mapperRegElem->buf,
      mapperRegElem->len,
      mapperRegElem->state);
  if (NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT >= 0) {
    this->numRegistrations--;
    recordRegistDuration(GlobalRegistDurationType::DEREG_MEM, dur.durationMs());
  }

  INFO(
      NCCL_COLL,
      "CTRAN-MAPPER: deregiter buffer %p len %ld",
      mapperRegElem->buf,
      mapperRegElem->len);

exit:
  return res;
}

ncclResult_t CtranMapper::regMem(
    const void* buf,
    std::size_t len,
    void** hdl,
    bool forceRegist) {
  ncclResult_t res = ncclSuccess;
  struct CtranMapperRegElem* mapperRegElem = nullptr;

  auto hdl_ = this->pimpl_->mapperRegElemList->search(buf, len);
  if (hdl_) {
    *hdl = hdl_;
    goto exit;
  }

  cudaPointerAttributes attr;
  CUDACHECKGOTO(cudaPointerGetAttributes(&attr, buf), res, exit);
  if (attr.type != cudaMemoryTypeDevice) {
    WARN("CTRAN-MAPPER: buf %p is not a device buffer\n", buf);
    res = ncclSystemError;
    goto exit;
  }

  /* create a new entry to cache the buffer info in the AVL tree */
  mapperRegElem = new struct CtranMapperRegElem;
  mapperRegElem->buf = buf;
  mapperRegElem->len = len;
  mapperRegElem->ibRegElem = nullptr;
  mapperRegElem->state = CtranMapperRegElemState::CACHED;

  *hdl = this->pimpl_->mapperRegElemList->insert(
      buf, len, reinterpret_cast<void*>(mapperRegElem));

  /* regiser the buffer only if on Eager mode or forced by caller */
  if (NCCL_CTRAN_REGISTER == NCCL_CTRAN_REGISTER::eager || forceRegist) {
    NCCLCHECKGOTO(this->pimpl_->regMem(mapperRegElem), res, fail);
  } else if (NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT >= 0) {
    // In lazy registration
    this->pimpl_->numCachedRegistrations++;
    this->pimpl_->totalNumCachedRegistrations++;
  }

exit:
  return res;
fail:
  if (*hdl) {
    this->pimpl_->mapperRegElemList->remove(*hdl);
  }
  delete mapperRegElem;
  goto exit;
}

ncclResult_t CtranMapper::deregMem(void* hdl) {
  ncclResult_t res = ncclSuccess;
  struct CtranMapperRegElem* mapperRegElem = nullptr;

  /* fast return for invalid handle: nullptr or cannot be found in the cache */
  if (hdl == nullptr ||
      !(mapperRegElem = reinterpret_cast<struct CtranMapperRegElem*>(
            this->pimpl_->mapperRegElemList->lookup(hdl)))) {
    return ncclSuccess;
  }

  if (mapperRegElem->state == CtranMapperRegElemState::REGISTERED) {
    NCCLCHECKGOTO(this->pimpl_->deregMem(mapperRegElem), res, exit);
  } else if (NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT >= 0) {
    // Just remove cache if the buffer is never registered
    this->pimpl_->numCachedRegistrations--;
  }

exit:
  return this->pimpl_->mapperRegElemList->remove(hdl);
}

ncclResult_t CtranMapper::searchRegHandle(
    const void* buf,
    std::size_t len,
    void** hdl,
    bool* dynamicRegist) {
  ncclResult_t res = ncclSuccess;
  // Determine whether the buffer has already registered
  bool lookupHit = true;
  auto dur = CtranMapperTimer();

  *hdl = this->pimpl_->mapperRegElemList->search(buf, len);

  if (*hdl != nullptr) {
    struct CtranMapperRegElem* mapperRegElem =
        reinterpret_cast<struct CtranMapperRegElem*>(
            this->pimpl_->mapperRegElemList->lookup(*hdl));

    // User has cached it but we delay the registration until now due to lazy
    // registration
    if (mapperRegElem->state == CtranMapperRegElemState::CACHED) {
      NCCLCHECKGOTO(this->pimpl_->regMem(mapperRegElem), res, exit);
      lookupHit = false;
    }
    *dynamicRegist = false;
  } else {
    // Oops, the buffer is not cached nor registered by user. Thus, we have to
    // register it on demand
    NCCLCHECKGOTO(
        this->regMem(buf, len, hdl, true /* force register */), res, exit);
    // caller is responsible for deregisgration
    *dynamicRegist = true;
    lookupHit = false;
  }

  if (NCCL_CTRAN_REGISTER_REPORT_SNAPSHOT_COUNT >= 0) {
    if (lookupHit) {
      recordRegistDuration(
          GlobalRegistDurationType::LOOKUP_HIT, dur.durationMs());
      this->pimpl_->totalNumRegLookupHit++;
    } else {
      recordRegistDuration(
          GlobalRegistDurationType::LOOKUP_MISS, dur.durationMs());
      this->pimpl_->totalNumRegLookupMiss++;
      if (*dynamicRegist) {
        this->pimpl_->totalNumDynamicRegistrations++;
      } else {
        this->pimpl_->numCachedRegistrations--;
      }
    }
  }

exit:
  return res;
}

ncclResult_t CtranMapper::icopy(
    void* dbuf,
    const void* sbuf,
    std::size_t len,
    CtranMapperRequest** req) {
  ncclResult_t res = ncclSuccess;

  *req = new CtranMapperRequest(this);
  CUDACHECKGOTO(
      cudaMemcpyAsync(dbuf, sbuf, len, cudaMemcpyDefault, this->internalStream),
      res,
      exit);

exit:
  return res;
}

ncclResult_t CtranMapper::icopy(
    void* dbuf,
    const void* sbuf,
    std::size_t len,
    cudaStream_t stream,
    CtranMapperRequest** req) {
  ncclResult_t res = ncclSuccess;

  *req = new CtranMapperRequest(this);
  CUDACHECKGOTO(
      cudaMemcpyAsync(dbuf, sbuf, len, cudaMemcpyDefault, stream),
      res,
      exit);

exit:
  return res;
}

ncclResult_t CtranMapper::progress(void) {
  ncclResult_t res = ncclSuccess;

  if (this->pimpl_->ctranIb != nullptr) {
    NCCLCHECKGOTO(this->pimpl_->ctranIb->progress(), res, exit);
  }

exit:
  return res;
}

ncclResult_t CtranMapper::isendCtrl(
    void* buf,
    void* hdl,
    int rank,
    CtranMapperRequest** req) {
  ncclResult_t res = ncclSuccess;

  if (this->pimpl_->ctranIb != nullptr) {
    struct CtranMapperRegElem* mapperRegElem =
        reinterpret_cast<struct CtranMapperRegElem*>(
            this->pimpl_->mapperRegElemList->lookup(hdl));

    CtranIbRequest** ibReqPtr = nullptr;
    if (req) {
      *req = new CtranMapperRequest(this);
      ibReqPtr = &((*req)->ibReq);
    }
    res = this->pimpl_->ctranIb->isendCtrl(
        buf, mapperRegElem->ibRegElem, rank, ibReqPtr);
  }

  return res;
}

ncclResult_t CtranMapper::irecvCtrl(
    void** buf,
    struct CtranMapperRemoteAccessKey* key,
    int rank,
    CtranMapperRequest** req) {
  ncclResult_t res = ncclSuccess;

  if (this->pimpl_->ctranIb != nullptr) {
    CtranIbRequest** ibReqPtr = nullptr;
    if (req) {
      *req = new CtranMapperRequest(this);
      ibReqPtr = &((*req)->ibReq);
    }
    res = this->pimpl_->ctranIb->irecvCtrl(buf, &key->ibKey, rank, ibReqPtr);
  }

  return res;
}

ncclResult_t CtranMapper::iput(
    const void* sbuf,
    void* dbuf,
    std::size_t len,
    int rank,
    void* shdl,
    struct CtranMapperRemoteAccessKey remoteAccessKey,
    bool notify,
    CtranMapperRequest** req) {
  ncclResult_t res = ncclSuccess;

  if (this->pimpl_->ctranIb != nullptr) {
    struct CtranMapperRegElem* mapperRegElem =
        reinterpret_cast<struct CtranMapperRegElem*>(
            this->pimpl_->mapperRegElemList->lookup(shdl));
    CtranIbRequest** ibReqPtr = nullptr;
    if (req) {
      *req = new CtranMapperRequest(this);
      ibReqPtr = &((*req)->ibReq);
    }
    this->pimpl_->ctranIb->iput(
        sbuf,
        dbuf,
        len,
        rank,
        mapperRegElem->ibRegElem,
        remoteAccessKey.ibKey,
        notify,
        ibReqPtr);
  }

  return res;
}

ncclResult_t CtranMapper::checkNotify(int rank, bool* notify) {
  ncclResult_t res = ncclSuccess;

  if (this->pimpl_->ctranIb) {
    res = this->pimpl_->ctranIb->checkNotify(rank, notify);
  }

  return res;
}

ncclResult_t CtranMapper::waitNotify(int rank) {
  ncclResult_t res = ncclSuccess;

  bool notify = false;
  while (!notify) {
    NCCLCHECKGOTO(this->checkNotify(rank, &notify), res, exit);
  }

exit:
  return res;
}
