// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SCAN_PORT_H
#define SCAN_PORT_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_BANNER_LEN 64       
#define CONNECT_TIMEOUT_S 1   // seconds
#define UDP_TIMEOUT_MS 500   // miliseconds 
#define MAX_IP_RANGE_SPAN 256

typedef enum {
  PROTO_TCP,
  PROTO_UDP
} scan_protocol_t;

typedef enum {
  STATUS_OPEN,            
  STATUS_OPEN_FILTERED    
} port_status_t;

typedef struct {
  char ip_str[16];                
  int port;                       
  scan_protocol_t protocol;       
  port_status_t status;           
  char banner[MAX_BANNER_LEN];    
} scan_result_t;



// Unic target scan(level 1)
int port_scan_target_range(const char *target_ip, int start_port, int end_port, scan_result_t *results, int max_results);
int port_scan_target_list(const char *target_ip, const int *port_list, int list_size, scan_result_t *results, int max_results);

// Target range scan(level 2)
int port_scan_network_range_using_port_range(const char *start_ip, const char *end_ip, int start_port, int end_port, scan_result_t *results, int max_results);
int port_scan_network_range_using_port_list(const char *start_ip, const char *end_ip, const int *port_list, int list_size, scan_result_t *results, int max_results);

// Target scan with CIDR (level 3)
int port_scan_cidr_using_port_range(const char *base_ip, int cidr, int start_port, int end_port, scan_result_t *results, int max_results);
int port_scan_cidr_using_port_list(const char *base_ip, int cidr, const int *port_list, int list_size, scan_result_t *results, int max_results);

#endif
