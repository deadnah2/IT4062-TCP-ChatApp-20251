/*
 * client/client_main.c
 * - Main entry point của client.
 * - Parse command line arguments (IP, port).
 * - Kết nối đến server qua TCP.
 * - Vòng lặp chính: hiển thị menu, nhận input, gọi handler tương ứng.
 */

#include "client.h"

int main(int argc, char **argv)
{
    // Kiểm tra arguments: cần IP và port
    if (argc < 3) {
        printf("Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    unsigned short port = (unsigned short)atoi(argv[2]);

    // Kết nối TCP đến server
    int sock = client_connect(ip, port);
    if (sock < 0) {
        printf("Failed to connect\n");
        return 1;
    }

    // Khởi tạo client state
    ClientState cs;
    cs.sock = sock;
    framer_init(&cs.framer, 2048);  // Buffer 2KB cho framing
    cs.token[0] = '\0';             // Chưa login nên token rỗng
    cs.next_id = 1;                 // Request ID bắt đầu từ 1

    // Vòng lặp chính
    for (;;) {
        menu_show(cs.token[0] != 0);  // Hiển thị menu (khác nhau tùy login state)
        printf("> ");
        fflush(stdout);

        // Đọc lựa chọn từ user
        int choice = 0;
        if (scanf("%d", &choice) != 1) {
            // Input không hợp lệ, clear buffer
            while (getchar() != '\n')
                ;
            continue;
        }
        while (getchar() != '\n')
            ;

        // Dispatch theo choice
        switch (choice) {
        case 0:
            cmd_disconnect(&cs);
            goto cleanup;

        case 1:
            cmd_register(&cs);
            break;

        case 2:
            cmd_login(&cs);
            break;

        case 3:
            cmd_whoami(&cs);
            break;

        case 4:
            cmd_raw_send(&cs);
            break;

        case 5:
            cmd_logout(&cs);
            break;

        case 6:
            cmd_friend_invite(&cs);
            break;

        case 7:
            cmd_friend_pending(&cs);
            break;

        case 8:
            cmd_friend_list(&cs);
            break;

        case 9:
            cmd_groups_menu(&cs);
            break;

        case 10:
            cmd_chat_mode(&cs);
            break;

        default:
            printf("Invalid choice\n");
            break;
        }
    }

cleanup:
    // Dọn dẹp tài nguyên
    framer_free(&cs.framer);
    close(sock);
    return 0;
}
