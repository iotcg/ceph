// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright(c) 2021 Liu, Changcheng <changcheng.liu@aliyun.com>
 *
 */

#ifndef CEPH_MSG_UCXSTACK_H
#define CEPH_MSG_UCXSTACK_H

#include <sys/eventfd.h>
#include <ucp/api/ucp.h>
#include <ucs/datastruct/list.h>

#include <list>
#include <vector>
#include <thread>

#include "common/ceph_context.h"
#include "common/debug.h"
#include "common/errno.h"
#include "msg/async/Stack.h"

class UCXWorker;

/*
 * UCX callback for send/receive completion
 */
class UcxCallback {
public:
    virtual ~UcxCallback();
    virtual void operator()(ucs_status_t status) = 0;
};

struct ucx_request {
    UcxCallback *callback;
    ucs_status_t status;
    bool completed;
    uint32_t conn_id;
    size_t recv_length;
    ucs_list_link_t pos;
};

class UCXProEngine {
private:
  ucp_worker_h ucp_worker;
public:
  UCXProEngine(ucp_worker_h ucp_worker)
  {
  }
};

class UCXConSktImpl : public ConnectedSocketImpl {
protected:
  CephContext *cct;
  UCXWorker* ucx_worker;
  int connected = -1;
  int tcp_fd = -1;
  int event_fd = -1;

  ceph::mutex lock = ceph::make_mutex("UCXConSktImpl::lock");
  bool is_server;
  bool active;
  bool pending;

public:
  UCXConSktImpl(CephContext *cct, UCXWorker *ucx_worker);
  ~UCXConSktImpl();

  int is_connected() override;
  ssize_t read(char* buf, size_t len) override;
  ssize_t send(ceph::bufferlist &bl, bool more) override;
  void shutdown() override;
  void close() override;
  int fd() const override;
};

class UCXSerSktImpl : public ServerSocketImpl {
protected:
  CephContext *cct;
  int listen_socket = -1;
  UCXWorker *ucx_worker;
  entity_addr_t listen_addr;

public:
  UCXSerSktImpl(CephContext *cct, UCXWorker *ucx_worker,
                entity_addr_t& listen_addr, unsigned addr_slot);

  int listen(entity_addr_t &listen_addr, const SocketOptions &skt_opts);

  int accept(ConnectedSocket *peer_socket, const SocketOptions &opts,
             entity_addr_t *peer_addr, Worker *ucx_worker) override;
  void abort_accept() override;
  int fd() const override;
};

class UCXWorker : public Worker {
  std::shared_ptr<UCXProEngine> ucp_worker_engine;
  std::list<UCXConSktImpl*> pending_sent_conns;
  ceph::mutex lock = ceph::make_mutex("UCXWorker::lock");

public:
  explicit
  UCXWorker(CephContext *cct, unsigned worker_id,
            std::shared_ptr<UCXProEngine> ucp_worker_engine);
  ~UCXWorker();

  int listen(entity_addr_t &listen_addr, unsigned addr_slot,
             const SocketOptions &skt_opts, ServerSocket *ser_skt) override;
  int connect(const entity_addr_t &peer_addr,
              const SocketOptions &peer_opts,
              ConnectedSocket *peer_skt) override;
  void destroy() override;
  void initialize() override;

  void remove_pending_conn(UCXConSktImpl *remove_obj) {
    ceph_assert(center.in_thread());
    pending_sent_conns.remove(remove_obj);
  }
};

class UCXStack : public NetworkStack {
private:
  ucp_context_h ucp_ctx;
  std::shared_ptr<UCXProEngine> ucp_worker_engine;
  std::vector<std::thread> worker_threads;
  Worker* create_worker(CephContext *cct, unsigned worker_id) override;

public:
  explicit UCXStack(CephContext *cct);
  ~UCXStack();

  void spawn_worker(std::function<void ()> &&worker_func) override;
  void join_worker(unsigned idx) override;

  static void request_init(void *request);
  static void request_reset(ucx_request *r);
  static void request_release(void *request);

};

#endif //CEPH_MSG_UCXSTACK_H
