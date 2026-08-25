// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef PORT_SCAN_H
#define PORT_SCAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define PORT_SCAN_MAX_BANNER_LEN    64
#define PORT_SCAN_CONNECT_TIMEOUT_S 1
#define PORT_SCAN_UDP_TIMEOUT_MS    500
#define PORT_SCAN_MAX_IP_RANGE_SPAN 256

/**
 * @brief Transport protocol for a port scan.
 */
typedef enum {
  PORT_SCAN_PROTO_TCP = 0,
  PORT_SCAN_PROTO_UDP,
  PORT_SCAN_PROTO_COUNT
} port_scan_protocol_t;

/**
 * @brief Status of a scanned port.
 */
typedef enum {
  PORT_SCAN_STATUS_OPEN = 0,
  PORT_SCAN_STATUS_OPEN_FILTERED,
  PORT_SCAN_STATUS_COUNT
} port_scan_status_t;

/**
 * @brief Result of a single port scan.
 */
typedef struct {
  char ip_str[16];
  int port;
  port_scan_protocol_t protocol;
  port_scan_status_t status;
  char banner[PORT_SCAN_MAX_BANNER_LEN];
} port_scan_result_t;

/**
 * @brief Scan a single target over a port range (TCP and UDP).
 *
 * @param target_ip    Target IP address string.
 * @param start_port   First port to scan.
 * @param end_port     Last port to scan.
 * @param results      Output array for results.
 * @param max_results  Maximum number of results to store.
 * @return Number of open ports found.
 */
int port_scan_target_range(const char *target_ip,
                           int start_port,
                           int end_port,
                           port_scan_result_t *results,
                           int max_results);

/**
 * @brief Scan a single target over a list of ports (TCP and UDP).
 *
 * @param target_ip    Target IP address string.
 * @param port_list    Array of ports to scan.
 * @param list_size    Number of entries in port_list.
 * @param results      Output array for results.
 * @param max_results  Maximum number of results to store.
 * @return Number of open ports found.
 */
int port_scan_target_list(const char *target_ip,
                          const int *port_list,
                          int list_size,
                          port_scan_result_t *results,
                          int max_results);

/**
 * @brief Scan a range of IPs over a port range.
 *
 * @param start_ip     First IP address in the range.
 * @param end_ip       Last IP address in the range.
 * @param start_port   First port to scan.
 * @param end_port     Last port to scan.
 * @param results      Output array for results.
 * @param max_results  Maximum number of results to store.
 * @return Number of open ports found.
 */
int port_scan_network_range_using_port_range(const char *start_ip,
                                             const char *end_ip,
                                             int start_port,
                                             int end_port,
                                             port_scan_result_t *results,
                                             int max_results);

/**
 * @brief Scan a range of IPs over a list of ports.
 *
 * @param start_ip     First IP address in the range.
 * @param end_ip       Last IP address in the range.
 * @param port_list    Array of ports to scan.
 * @param list_size    Number of entries in port_list.
 * @param results      Output array for results.
 * @param max_results  Maximum number of results to store.
 * @return Number of open ports found.
 */
int port_scan_network_range_using_port_list(const char *start_ip,
                                            const char *end_ip,
                                            const int *port_list,
                                            int list_size,
                                            port_scan_result_t *results,
                                            int max_results);

/**
 * @brief Scan a CIDR network over a port range.
 *
 * @param base_ip      Base IP address of the CIDR block.
 * @param cidr         CIDR prefix length.
 * @param start_port   First port to scan.
 * @param end_port     Last port to scan.
 * @param results      Output array for results.
 * @param max_results  Maximum number of results to store.
 * @return Number of open ports found.
 */
int port_scan_cidr_using_port_range(const char *base_ip,
                                    int cidr,
                                    int start_port,
                                    int end_port,
                                    port_scan_result_t *results,
                                    int max_results);

/**
 * @brief Scan a CIDR network over a list of ports.
 *
 * @param base_ip      Base IP address of the CIDR block.
 * @param cidr         CIDR prefix length.
 * @param port_list    Array of ports to scan.
 * @param list_size    Number of entries in port_list.
 * @param results      Output array for results.
 * @param max_results  Maximum number of results to store.
 * @return Number of open ports found.
 */
int port_scan_cidr_using_port_list(const char *base_ip,
                                   int cidr,
                                   const int *port_list,
                                   int list_size,
                                   port_scan_result_t *results,
                                   int max_results);

/**
 * @brief Request an in-flight scan to abort at the next port boundary.
 *
 * The scans run on the shared async runner; this lets a STOP command cut a long
 * range/network sweep short without blocking. Loops check the flag between ports.
 */
void port_scan_request_abort(void);

/**
 * @brief Clear the abort flag before starting a new scan.
 */
void port_scan_reset_abort(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_SCAN_H
