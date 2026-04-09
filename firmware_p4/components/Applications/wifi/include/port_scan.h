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

#ifndef PORT_SCAN_H
#define PORT_SCAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define PORT_SCAN_MAX_BANNER_LEN    64
#define PORT_SCAN_CONNECT_TIMEOUT_S 1
#define PORT_SCAN_UDP_TIMEOUT_MS    500
#define PORT_SCAN_MAX_IP_RANGE      256

/**
 * @brief Scan protocol type.
 */
typedef enum {
  PORT_SCAN_PROTO_TCP = 0,
  PORT_SCAN_PROTO_UDP,
  PORT_SCAN_PROTO_COUNT
} port_scan_protocol_t;

/**
 * @brief Port scan result status.
 */
typedef enum {
  PORT_SCAN_STATUS_OPEN = 0,
  PORT_SCAN_STATUS_OPEN_FILTERED,
  PORT_SCAN_STATUS_COUNT
} port_scan_status_t;

/**
 * @brief Single port scan result entry.
 */
typedef struct {
  char ip_str[16];
  int port;
  port_scan_protocol_t protocol;
  port_scan_status_t status;
  char banner[PORT_SCAN_MAX_BANNER_LEN];
} port_scan_result_t;

/**
 * @brief Scan a port range on a single target.
 *
 * @param target_ip    Target IP address string.
 * @param start_port   First port in the range.
 * @param end_port     Last port in the range.
 * @param results      Output array for results.
 * @param max_results  Maximum entries in the results array.
 * @return Number of open ports found.
 */
int port_scan_target_range(const char *target_ip,
                           int start_port,
                           int end_port,
                           port_scan_result_t *results,
                           int max_results);

/**
 * @brief Scan a list of specific ports on a single target.
 *
 * @param target_ip    Target IP address string.
 * @param port_list    Array of ports to scan.
 * @param list_size    Number of entries in port_list.
 * @param results      Output array for results.
 * @param max_results  Maximum entries in the results array.
 * @return Number of open ports found.
 */
int port_scan_target_list(const char *target_ip,
                          const int *port_list,
                          int list_size,
                          port_scan_result_t *results,
                          int max_results);

/**
 * @brief Scan a port range across a network IP range.
 *
 * @param start_ip     First IP in the range.
 * @param end_ip       Last IP in the range.
 * @param start_port   First port in the range.
 * @param end_port     Last port in the range.
 * @param results      Output array for results.
 * @param max_results  Maximum entries in the results array.
 * @return Number of open ports found.
 */
int port_scan_network_range_using_port_range(const char *start_ip,
                                             const char *end_ip,
                                             int start_port,
                                             int end_port,
                                             port_scan_result_t *results,
                                             int max_results);

/**
 * @brief Scan a list of ports across a network IP range.
 *
 * @param start_ip     First IP in the range.
 * @param end_ip       Last IP in the range.
 * @param port_list    Array of ports to scan.
 * @param list_size    Number of entries in port_list.
 * @param results      Output array for results.
 * @param max_results  Maximum entries in the results array.
 * @return Number of open ports found.
 */
int port_scan_network_range_using_port_list(const char *start_ip,
                                            const char *end_ip,
                                            const int *port_list,
                                            int list_size,
                                            port_scan_result_t *results,
                                            int max_results);

/**
 * @brief Scan a port range across a CIDR subnet.
 *
 * @param base_ip      Base IP address of the subnet.
 * @param cidr         CIDR prefix length.
 * @param start_port   First port in the range.
 * @param end_port     Last port in the range.
 * @param results      Output array for results.
 * @param max_results  Maximum entries in the results array.
 * @return Number of open ports found.
 */
int port_scan_cidr_using_port_range(const char *base_ip,
                                    int cidr,
                                    int start_port,
                                    int end_port,
                                    port_scan_result_t *results,
                                    int max_results);

/**
 * @brief Scan a list of ports across a CIDR subnet.
 *
 * @param base_ip      Base IP address of the subnet.
 * @param cidr         CIDR prefix length.
 * @param port_list    Array of ports to scan.
 * @param list_size    Number of entries in port_list.
 * @param results      Output array for results.
 * @param max_results  Maximum entries in the results array.
 * @return Number of open ports found.
 */
int port_scan_cidr_using_port_list(const char *base_ip,
                                   int cidr,
                                   const int *port_list,
                                   int list_size,
                                   port_scan_result_t *results,
                                   int max_results);

#ifdef __cplusplus
}
#endif

#endif // PORT_SCAN_H
