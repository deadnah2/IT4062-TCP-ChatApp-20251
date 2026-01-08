/**
 * client/client_ui.c
 * Hiển thị menu và giao diện người dùng
 * 
 * Chức năng:
 * - menu_show(): Hiển thị menu chính với các lựa chọn
 * - Menu khác nhau tùy theo trạng thái đăng nhập
 */

#include "client.h"

/**
 * menu_show
 * Hiển thị menu chính của client
 * @param logged_in: 1 nếu đã đăng nhập, 0 nếu chưa
 * 
 * Khi chưa đăng nhập: Hiển thị Register, Login
 * Khi đã đăng nhập: Hiển thị đầy đủ các chức năng
 */
void menu_show(int logged_in)
{
    printf("\n" C_TITLE "══════════════════════════════════\n");
    printf("        💬 CHAT CLIENT MENU        \n");
    printf("══════════════════════════════════\n" C_RESET);

    // Chỉ hiển thị Register/Login khi chưa đăng nhập
    if (!logged_in) {
        printf(C_MENU " 1. " ICON_USER " Register\n");
        printf(" 2. " ICON_LOGIN " Login\n");
    }
    
    // Các lệnh chung
    printf(C_MENU " 3. " ICON_ID " Whoami\n");
    printf(" 4. " ICON_RAW " Raw send\n");

    // Các lệnh chỉ hiện khi đã đăng nhập
    if (logged_in) {
        printf(" 5. " ICON_LOGOUT " Logout\n");
        printf(" 6. " ICON_INVITE " Add friend (send invite)\n");
        printf(" 7. " ICON_LIST " View friend invites\n");
        printf(" 8. " ICON_FRIEND " View friend list\n");
        printf(" 9. " ICON_GROUP " Group\n");
        printf("10. " ICON_CHAT " Chat (Private Message)\n");
    }

    printf(" 0. " ICON_EXIT " Exit\n");
    printf(C_TITLE "══════════════════════════════════\n" C_RESET);

    // Hiển thị trạng thái đăng nhập
    if (logged_in)
        printf(C_OK "✔ Logged in\n" C_RESET);
    else
        printf(C_DIM "Not logged in\n" C_RESET);
}
