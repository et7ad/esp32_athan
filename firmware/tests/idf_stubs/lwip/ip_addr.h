#pragma once
#include "idf_common.h"
#define LWIP_IPV6 0
typedef struct { uint32_t addr; } ip4_addr_t;
typedef ip4_addr_t ip_addr_t;
#define IPADDR_TYPE_V4 0
#define ip_addr_set_zero(a) ((a)->addr = 0)
#define IP_ADDR4(a, b, c, d, e) ((a)->addr = ((uint32_t)(b)) | ((uint32_t)(c) << 8) | ((uint32_t)(d) << 16) | ((uint32_t)(e) << 24))
#define ip_addr_copy(dst, src) ((dst) = (src))
int ipaddr_aton(const char *, ip_addr_t *);
#define ip_addr_isany(a) ((a) == nullptr || (a)->addr == 0)
#define ip_addr_cmp(a, b) ((a)->addr == (b)->addr)
#define ip_addr_get_ip4_u32(a) ((a)->addr)
#define ip_addr_ismulticast(a) (false)
#define IP_IS_V4(a) (true)
#define IP_IS_V6(a) (false)
char *ipaddr_ntoa_r(const ip_addr_t *, char *, int);
#define ip_addr_set_ip4_u32(a, v) ((a)->addr = (v))
#define ip4_addr_get_u32(a) ((a)->addr)
typedef uint8_t u8_t;
#define ip4_addr_get_byte(a, i) ((u8_t)(((a)->addr >> ((i) * 8)) & 0xff))
