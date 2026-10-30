#pragma once
#ifdef HOST_THREADED
#include "ThreadHost.hpp"
#else
#include "Host.hpp"
#endif
#define socket host_socket
#define setsockopt host_setsockopt
#define bind host_bind
#define close host_close
#define shutdown host_shutdown
#define recvfrom host_recvfrom
#define sendto host_sendto
