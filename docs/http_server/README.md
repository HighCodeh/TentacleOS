# HTTP Server Service Component Documentation

This component provides an abstraction layer over ESP-IDF's native `esp_http_server`, facilitating initialization, request handling, response sending, and file system (SD Card) integration for the Highboy project.

## Overview

- **Location:** `components/Service/http_server/`
- **Main Header:** `include/http_server_service.h`
- **Implementation:** `http_server_service.c`

The service manages the web server lifecycle (start/stop), route registration (URIs), and offers utilities for reading HTML files from storage and handling standard HTTP errors.

## API Functions

### Server Management

#### `start_web_server`
```c
esp_err_t start_web_server(void);
```
Starts the HTTP server with default configurations, enabling `lru_purge_enable` to manage old connections.

#### `stop_http_server`
```c
esp_err_t stop_http_server(void);
```
Stops the HTTP server if it is running and frees associated resources.

#### `http_service_register_uri`
```c
esp_err_t http_service_register_uri(const httpd_uri_t *uri_handler);
```
Registers a URI handler (route) on the active server. Returns an error if the server is not started.

### Request and Response Handling

#### `http_service_req_recv`
```c
esp_err_t http_service_req_recv(httpd_req_t *req, char *buffer, size_t buffer_size);
```
Receives the content (body) of a request with safety checks for buffer size.
- Returns `ESP_ERR_INVALID_SIZE` if the content is larger than the buffer.
- Automatically handles timeouts.

#### `http_service_query_key_value`
```c
esp_err_t http_service_query_key_value(const char *data_buffer, const char *key, char *out_val, size_t out_size);
```
Extracts the value of a specific key from a query string (URL encoded). Handles cases where the key is not found or the value is truncated.

#### `http_service_send_response`
```c
esp_err_t http_service_send_response(httpd_req_t *req, const char *buffer, ssize_t length);
```
Sends a generic HTTP response.
- If `buffer` is `NULL`, it automatically sends a 500 error.

#### `http_service_send_error`
```c
esp_err_t http_service_send_error(httpd_req_t *req, http_status_t status_code, const char *msg);
```
Sends a standardized HTTP error response, mapping the internal `http_status_t` enum to ESP-IDF error codes (`httpd_err_code_t`).

### Storage Integration (SD Card)

#### `get_html_buffer`
```c
const char *get_html_buffer(const char *path);
```
Reads an entire file from the specified path (usually from the SD Card) and returns a dynamically allocated buffer containing the data, null-terminated (`\0`).
- **Note:** The caller is responsible for freeing the returned memory (see Casting note below).

#### `http_service_send_file_from_sd`
```c
esp_err_t http_service_send_file_from_sd(httpd_req_t *req, const char *filepath);
```
Combines `get_html_buffer` and `http_service_send_response` to read a file and send it directly as a response to the request. Automatically frees the buffer memory after sending.

---

## Castings and Implementation Details

Below are listed all explicit "castings" (type conversions) performed in the source code `http_server_service.c`, which are fundamental for memory allocation and opaque type manipulation.

### 1. File Buffer Allocation
**Location:** Function `get_html_buffer`
```c
char *buffer = (char *)malloc(file_size + 1);
```
- **From:** `void *` (generic return from `malloc`)
- **To:** `char *`
- **Reason:** The pointer returned by `malloc` needs to be treated as a character string to store the file content and the null terminator.

### 2. Constant Memory Deallocation
**Location:** Function `http_service_send_file_from_sd`
```c
free((void*)html_content);
```
- **From:** `const char *` (type of `html_content` variable)
- **To:** `void *`
- **Reason:** The `get_html_buffer` function returns a `const char *` to semantically indicate that the receiver should not alter its content. However, to free this memory with `free()`, it is necessary to remove the `const` qualifier via a cast to `void *`; otherwise, the compiler would emit a warning or error, since `free` expects a pointer to mutable memory (even though it only frees it).

---

## Auxiliary Data Structures

### `http_status_t`
Enumeration defined in `http_server_service.h` to abstract HTTP status codes and facilitate internal mapping:
- `HTTP_STATUS_OK_200`
- `HTTP_STATUS_CREATED_201`
- `HTTP_STATUS_BAD_REQUEST_400`
- `HTTP_STATUS_UNAUTHORIZED_401`
- `HTTP_STATUS_FORBIDDEN_403`
- `HTTP_STATUS_NOT_FOUND_404`
- `HTTP_STATUS_REQUEST_TIMEOUT_408`
- `HTTP_STATUS_INTERNAL_ERROR_500`
