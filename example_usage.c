#include "ws_lib.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

int main() {
    if (ws_init() != 0) {
        printf("WebSocket initialization failed.\n");
        return -1;
    }

    ws_ctx* ctx = ws_create_ctx();
    if (!ctx) {
        printf("Failed to create WebSocket context.\n");
        ws_cleanup();
        return -1;
    }

    printf("Connecting to WebSocket server...\n");
    if (ws_connect(ctx, "ws://localhost:8765") != 0) {
        printf("WebSocket connection failed.\n");
        ws_destroy_ctx(ctx);
        ws_cleanup();
        return -1;
    }

    printf("WebSocket connected.\n");

    const char* message = "Hello, WebSocket!";
    printf("Sending message: %s\n", message);
    if (ws_send(ctx, message, strlen(message), WS_OPCODE_TEXT) != 0) {
        printf("Failed to send message.\n");
    } else {
        printf("Message sent successfully.\n");
    }

    char recv_buffer[1024];
    int recv_len;

    printf("Waiting for response...\n");
    for (int i = 0; i < 10; i++) {
        recv_len = ws_recv(ctx, recv_buffer, sizeof(recv_buffer));
        if (recv_len > 0) {
            printf("Received: %.*s\n", recv_len, recv_buffer);
            break;
        } else if (recv_len == 0) {
            printf("No data received, retrying...\n");
            Sleep(100); // Wait a bit before trying again
        } else {
            printf("Error receiving data.\n");
            break;
        }
    }

    printf("Closing WebSocket connection...\n");
    ws_close(ctx);
    ws_destroy_ctx(ctx);
    ws_cleanup();

    printf("WebSocket connection closed.\n");
    return 0;
}
