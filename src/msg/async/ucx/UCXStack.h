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
#include "msg/msg_types.h"
#include "msg/async/Stack.h"

class UCXWorker;
class UCXConSktImpl;

typedef enum {
  UNKNOWN,
  IO_READ,
  IO_WRITE,
  IO_READ_COMP,
  IO_WRITE_COMP
} io_op_t;

constexpr const char *io_op_names[] = {
  "unknown",
  "read",
  "write",
  "read completion",
  "write completion"
};

typedef enum {
  WAIT_STATUS_OK,
  WAIT_STATUS_FAILED,
  WAIT_STATUS_TIMED_OUT
} wait_status_t;

typedef struct {
  uint64_t sn;
  uint32_t data_size;
  uint32_t op_code;
} iomsg_t;

struct conn_req_t {
  ucp_conn_request_h conn_request;
  struct timeval     arrival_time;
};


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
  CephContext *cct;
  ceph::mutex lock = ceph::make_mutex("UCXProEngine::lock");
  std::thread thread_engine;
  ucp_worker_h ucp_worker;
  std::map<uint64_t, UCXConSktImpl*> ucx_connections;
  int engine_status;
  void dispatch_am_message(UCXConSktImpl *ucx_conn,
                           const void *header, size_t header_length,
                           void *data, const ucp_am_recv_param_t *param);
public:
  UCXProEngine(CephContext *cct, ucp_worker_h ucp_worker);
  ~UCXProEngine();

  void fire_polling();
  void progress();
  ucp_worker_h get_ucp_worker();
  void add_connections(uint64_t conn_id, UCXConSktImpl* ucx_conn);

  static ucs_status_t
  am_recv_callback(void *arg, const void *header,
                   size_t header_length,
                   void *data, size_t length,
                   const ucp_am_recv_param_t *param);
  wait_status_t
  wait_completion(ucs_status_ptr_t status_ptr, double timeout = 1e6);
};

class UCXConSktImpl : public ConnectedSocketImpl {
public:
  CephContext *cct;
private:
  UCXWorker* ucx_worker;
  std::shared_ptr<UCXProEngine> ucp_worker_engine;
  conn_req_t conn_request;
  bool active;
  int err_con = 0;

  int connected = -1;
  int data_event_fd = -1;
  uint64_t conn_id = -1;
  ucp_ep_h conn_ep = NULL;
  std::atomic_uint64_t sn_send{0};
  std::atomic_uint64_t sn_recv{0};

  bufferlist recv_pending_bl;
  std::queue<bufferlist> queue_bl;
  bufferlist send_pending_bl;

  ceph::mutex send_lock = ceph::make_mutex("UCXConSktImpl::send_lock");
  ceph::mutex recv_lock = ceph::make_mutex("UCXConSktImpl::recv_lock");

  void handle_connection_error(ucs_status_t status);
  static
  void ep_error_cb(void *arg, ucp_ep_h ep, ucs_status_t status);
  static
  void am_data_recv_callback(void *request, ucs_status_t status,
                             size_t length, void *user_data);
  static
  void am_data_send_callback(void *request, ucs_status_t status,
                             void *user_data);

public:
  UCXConSktImpl(CephContext *cct, UCXWorker *ucx_worker,
                std::shared_ptr<UCXProEngine> ucp_worker_engine);
  ~UCXConSktImpl();

  int is_connected() override;
  void set_connection_status(int con_status);
  void set_active_status(bool active_status);

  ssize_t read(char* buf, size_t len) override;
  ssize_t send(ceph::bufferlist &bl, bool more) override;
  void shutdown() override;
  void close() override;
  int fd() const override;
  void data_notify();

  void set_conn_request(const conn_req_t &conn_request);
  ucs_status_t create_server_ep();
  int client_start_connect(const entity_addr_t &server_addr, const SocketOptions &opts);
  ssize_t send_segments();
  void handle_io_am_write_request(const iomsg_t *msg, void *data,
                                  const ucp_am_recv_param_t *param);
};

class UCXSerSktImpl : public ServerSocketImpl {
public:
  CephContext *cct;
private:
  UCXWorker *ucx_worker;
  std::shared_ptr<UCXProEngine> ucp_worker_engine;
  entity_addr_t listen_addr;
  std::deque<conn_req_t> conn_requests;
  ucp_listener_h ucp_ser_listener = nullptr;
  int listen_skt_notify_fd = -1;

  const std::string sockaddr_str(const struct sockaddr* saddr, size_t addrlen);

public:
  UCXSerSktImpl(CephContext *cct, UCXWorker *ucx_worker,
                std::shared_ptr<UCXProEngine> ucp_worker_engine,
                entity_addr_t& listen_addr, unsigned addr_slot);

  int listen(const SocketOptions &skt_opts);

  int accept(ConnectedSocket *ser_con_socket, const SocketOptions &opts,
             entity_addr_t *peer_addr, Worker *ucx_worker) override;
  void abort_accept() override;
  int fd() const override;
  void listen_notify();

  static void recv_req_con_cb(ucp_conn_request_h conn_req, void *arg);
};

class UCXWorker : public Worker {
private:
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
