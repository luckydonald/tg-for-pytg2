#ifndef __NET_H__
#define __NET_H__

#include <poll.h>
struct dc;
#include "queries.h"
#define TG_SERVER "173.240.5.1"
//#define TG_SERVER "95.142.192.66"
#define TG_APP_HASH "3bc14c6455ef1595ec86a125762c3aad"
#define TG_APP_ID 51

#define ACK_TIMEOUT 60
#define MAX_DC_ID 10

enum dc_state{
  st_init,
  st_reqpq_sent,
  st_reqdh_sent,
  st_client_dh_sent,
  st_authorized,
  st_error
} ;

struct connection;
struct connection_methods {
  int (*ready) (struct connection *c);
  int (*close) (struct connection *c);
  int (*execute) (struct connection *c, int op, int len);
};


#define MAX_DC_SESSIONS 3

struct session {
  struct dc *dc;
  long long session_id;
  int seq_no;
  struct connection *c;
  struct tree_int *ack_tree;
  struct event_timer ev;
};

struct dc {
  int id;
  int port;
  int flags;
  char *ip;
  char *user;
  struct session *sessions[MAX_DC_SESSIONS];
  char auth_key[256];
  long long auth_key_id;
  long long server_salt;

  int server_time_delta;
  double server_time_udelta;
};

#define DC_SERIALIZED_MAGIC 0x64582faa
struct dc_serialized {
  int magic;
  int port;
  char ip[64];
  char user[64];
  char auth_key[256];
  long long auth_key_id, server_salt;
  int authorized;
};

struct connection_buffer {
  void *start;
  void *end;
  void *rptr;
  void *wptr;
  struct connection_buffer *next;
};

enum conn_state {
  conn_none,
  conn_connecting,
  conn_ready,
  conn_failed,
  conn_stopped
};

struct connection {
  int fd;
  int ip;
  int port;
  int flags;
  enum conn_state state;
  int ipv6[4];
  struct connection_buffer *in_head;
  struct connection_buffer *in_tail;
  struct connection_buffer *out_head;
  struct connection_buffer *out_tail;
  int in_bytes;
  int out_bytes;
  int packet_num;
  int out_packet_num;
  struct connection_methods *methods;
  struct session *session;
  void *extra;
};

extern struct connection *Connections[];

int write_out (struct connection *c, const void *data, int len);
void flush_out (struct connection *c);
int read_in (struct connection *c, void *data, int len);

void create_all_outbound_connections (void);

struct connection *create_connection (const char *host, int port, struct session *session, struct connection_methods *methods);
int connections_make_poll_array (struct pollfd *fds, int max);
void connections_poll_result (struct pollfd *fds, int max);
void dc_create_session (struct dc *DC);
void insert_seqno (struct session *S, int seqno);
struct dc *alloc_dc (int id, char *ip, int port);

#define GET_DC(c) (c->session->dc)
#endif