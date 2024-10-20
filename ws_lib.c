#include "ws_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_HEADER_SIZE 14

struct ws_ctx {
    SOCKET socket;
    ws_state state;
    char* recv_buffer;
    size_t recv_buffer_size;
    size_t recv_buffer_len;
};

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* base64_encode(const unsigned char* input, int length) {
    int output_length = 4 * ((length + 2) / 3);
    char* encoded = (char*)malloc(output_length + 1);
    if (encoded == NULL) return NULL;

    int i, j;
    for (i = 0, j = 0; i < length;) {
        uint32_t octet_a = i < length ? input[i++] : 0;
        uint32_t octet_b = i < length ? input[i++] : 0;
        uint32_t octet_c = i < length ? input[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        encoded[j++] = base64_table[(triple >> 18) & 0x3F];
        encoded[j++] = base64_table[(triple >> 12) & 0x3F];
        encoded[j++] = base64_table[(triple >> 6) & 0x3F];
        encoded[j++] = base64_table[triple & 0x3F];
    }

    for (i = 0; i < (3 - length % 3) % 3; i++)
        encoded[output_length - 1 - i] = '=';

    encoded[output_length] = '\0';

    return encoded;
}

static int ws_send_handshake(ws_ctx* ctx, const char* host, const char* path) {
    char key[16];
    char encoded_key[25];
    char request[1024];
    int request_len;

    // Generate random key
    srand((unsigned int)time(NULL));
    for (int i = 0; i < 16; i++) {
        key[i] = rand() % 256;
    }
    char* base64_key = base64_encode(key, 16);
    strncpy(encoded_key, base64_key, 24);
    encoded_key[24] = '\0';
    free(base64_key);

    // Construct handshake request
    request_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, encoded_key);

    // Send handshake request
    if (send(ctx->socket, request, request_len, 0) != request_len) {
        return -1;
    }

    return 0;
}

static int ws_parse_handshake_response(ws_ctx* ctx) {
    char buffer[1024];
    int bytes_received = recv(ctx->socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        return -1;
    }
    buffer[bytes_received] = '\0';

    // Check for "HTTP/1.1 101" status
    if (strstr(buffer, "HTTP/1.1 101") == NULL) {
        return -1;
    }

    // Check for "Upgrade: websocket"
    if (strstr(buffer, "Upgrade: websocket") == NULL) {
        return -1;
    }

    // TODO: Verify Sec-WebSocket-Accept if needed

    ctx->state = WS_STATE_OPEN;
    return 0;
}

int ws_init(void) {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData);
}

ws_ctx* ws_create_ctx(void) {
    ws_ctx* ctx = (ws_ctx*)malloc(sizeof(ws_ctx));
    if (ctx) {
        memset(ctx, 0, sizeof(ws_ctx));
        ctx->state = WS_STATE_CLOSED;
        ctx->recv_buffer_size = 1024;
        ctx->recv_buffer = (char*)malloc(ctx->recv_buffer_size);
        ctx->recv_buffer_len = 0;
    }
    return ctx;
}

int ws_connect(ws_ctx* ctx, const char* uri) {
    // Parse URI
    char schema[10], host[256], path[256];
    int port;
    if (sscanf(uri, "%[^:]://%[^:/]:%d%s", schema, host, &port, path) < 3) {
        if (sscanf(uri, "%[^:]://%[^/]%s", schema, host, path) < 3) {
            return -1;
        }
        port = strcmp(schema, "wss") == 0 ? 443 : 80;
    }
    if (path[0] == '\0') {
        strcpy(path, "/");
    }

    // Resolve address
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &result) != 0) {
        return -1;
    }

    // Create socket and connect
    ctx->socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ctx->socket == INVALID_SOCKET) {
        freeaddrinfo(result);
        return -1;
    }

    if (connect(ctx->socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        closesocket(ctx->socket);
        freeaddrinfo(result);
        return -1;
    }

    freeaddrinfo(result);

    // Send WebSocket handshake
    ctx->state = WS_STATE_CONNECTING;
    if (ws_send_handshake(ctx, host, path) != 0) {
        closesocket(ctx->socket);
        return -1;
    }

    // Parse handshake response
    if (ws_parse_handshake_response(ctx) != 0) {
        closesocket(ctx->socket);
        return -1;
    }

    return 0;
}

// Add this function to generate a random 32-bit mask
static uint32_t generate_mask() {
    return ((uint32_t)rand() << 24) | ((uint32_t)rand() << 16) | ((uint32_t)rand() << 8) | (uint32_t)rand();
}

int ws_send(ws_ctx* ctx, const char* data, size_t length, int opcode) {
    if (ctx->state != WS_STATE_OPEN) {
        return -1;
    }

    // Construct WebSocket frame
    uint8_t header[14];  // Maximum header size (2 + 8 + 4)
    size_t header_size = 2;
    uint32_t mask = generate_mask();

    header[0] = 0x80 | (opcode & 0x0F);
    
    if (length <= 125) {
        header[1] = 0x80 | length;
    } else if (length <= 65535) {
        header[1] = 0x80 | 126;
        header[2] = (length >> 8) & 0xFF;
        header[3] = length & 0xFF;
        header_size += 2;
    } else {
        header[1] = 0x80 | 127;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (length >> ((7 - i) * 8)) & 0xFF;
        }
        header_size += 8;
    }

    // Add mask to header
    memcpy(header + header_size, &mask, 4);
    header_size += 4;

    // Send frame header
    if (send(ctx->socket, (char*)header, header_size, 0) != header_size) {
        return -1;
    }

    // Apply mask to data and send
    char* masked_data = malloc(length);
    if (!masked_data) return -1;
    
    for (size_t i = 0; i < length; i++) {
        masked_data[i] = data[i] ^ ((uint8_t*)&mask)[i % 4];
    }

    int result = send(ctx->socket, masked_data, length, 0);
    free(masked_data);

    return (result == length) ? 0 : -1;
}

int ws_recv(ws_ctx* ctx, char* buffer, size_t buffer_size) {
    if (ctx->state != WS_STATE_OPEN) {
        return -1;
    }

    uint8_t header[2];
    int bytes_received = recv(ctx->socket, (char*)header, 2, 0);
    if (bytes_received != 2) return -1;

    bool fin = header[0] & 0x80;
    int opcode = header[0] & 0x0F;
    bool masked = header[1] & 0x80;
    uint64_t payload_length = header[1] & 0x7F;

    if (payload_length == 126) {
        uint16_t extended_length;
        bytes_received = recv(ctx->socket, (char*)&extended_length, 2, 0);
        if (bytes_received != 2) return -1;
        payload_length = ntohs(extended_length);
    } else if (payload_length == 127) {
        uint64_t extended_length;
        bytes_received = recv(ctx->socket, (char*)&extended_length, 8, 0);
        if (bytes_received != 8) return -1;
        payload_length = ntohll(extended_length);
    }

    if (payload_length > buffer_size) {
        // Buffer too small
        return -1;
    }

    uint32_t mask = 0;
    if (masked) {
        bytes_received = recv(ctx->socket, (char*)&mask, 4, 0);
        if (bytes_received != 4) return -1;
    }

    bytes_received = recv(ctx->socket, buffer, payload_length, 0);
    if (bytes_received != payload_length) return -1;

    if (masked) {
        for (size_t i = 0; i < payload_length; i++) {
            buffer[i] ^= ((uint8_t*)&mask)[i % 4];
        }
    }

    return bytes_received;
}

int ws_close(ws_ctx* ctx) {
    if (ctx->state == WS_STATE_OPEN) {
        uint8_t close_frame[] = {0x88, 0x00};
        send(ctx->socket, (char*)close_frame, 2, 0);
        ctx->state = WS_STATE_CLOSING;
    }
    
    closesocket(ctx->socket);
    ctx->state = WS_STATE_CLOSED;
    return 0;
}

void ws_destroy_ctx(ws_ctx* ctx) {
    if (ctx) {
        if (ctx->recv_buffer) {
            free(ctx->recv_buffer);
        }
        free(ctx);
    }
}

void ws_cleanup(void) {
    WSACleanup();
}

ws_state ws_get_state(ws_ctx* ctx) {
    return ctx->state;
}

int ws_service(ws_ctx* ctx) {
    // TODO: Implement ping/pong, handle incoming frames, etc.
    return 0;
}
