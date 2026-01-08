/**
 * client/client_utils.c
 * Các hàm tiện ích cho client
 * 
 * Bao gồm:
 * - client_connect(): Kết nối TCP đến server
 * - trim_line(): Xóa ký tự \n\r cuối chuỗi
 * - send_line(): Gửi 1 dòng request theo framing protocol
 * - kv_get(): Parse key=value từ payload
 * - parse_response(): Parse response OK/ERR từ server
 * - base64_encode/decode(): Mã hóa/giải mã Base64 cho nội dung tin nhắn
 */

#include "client.h"

// ============ Base64 Tables ============
// Bảng encode: index 0-63 -> ký tự Base64

static const char b64_table[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const int b64_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

// ============ Connection ============

/**
 * client_connect
 * Tạo kết nối TCP đến server
 * @param ip: Địa chỉ IP của server
 * @param port: Port của server
 * @return: socket fd nếu thành công, -1 nếu lỗi
 */
int client_connect(const char *ip, unsigned short port)
{
    // Tạo socket TCP
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;

    // Cấu hình địa chỉ server
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    // Thực hiện kết nối
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(s);
        return -1;
    }

    return s;
}

// ============ String helpers ============

/**
 * trim_line
 * Xóa các ký tự \n và \r ở cuối chuỗi (từ fgets)
 */
void trim_line(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = 0;
        n--;
    }
}

/**
 * send_line
 * Gửi 1 dòng request theo framing protocol (line + \r\n)
 * @return: 0 nếu thành công, -1 nếu lỗi
 */
int send_line(int sock, const char *line)
{
    if (!line)
        return -1;
    if (send(sock, line, (int)strlen(line), 0) <= 0)
        return -1;
    if (send(sock, "\r\n", 2, 0) <= 0)
        return -1;
    return 0;
}

// ============ Key-Value Parser ============

/**
 * kv_get
 * Parse payload dạng "k=v k=v ..." và lấy value theo key
 * Ví dụ: payload="token=abc user_id=123", key="token" -> out="abc"
 * @return: 1 nếu tìm thấy, 0 nếu không tìm thấy
 */
int kv_get(const char *payload, const char *key, char *out, size_t out_cap)
{
    if (!payload || !key || !out || out_cap == 0)
        return 0;
    out[0] = 0;

    size_t key_len = strlen(key);
    const char *p = payload;

    // Duyệt qua từng token (phân tách bởi space)
    while (*p) {
        // Bỏ qua khoảng trắng
        while (*p == ' ')
            p++;
        if (!*p)
            break;

        // Tìm cuối token
        const char *token_end = strchr(p, ' ');
        size_t token_len = token_end ? (size_t)(token_end - p) : strlen(p);
        
        // Tìm dấu = trong token
        const char *eq = memchr(p, '=', token_len);
        if (eq) {
            size_t klen = (size_t)(eq - p);
            size_t vlen = token_len - klen - 1;
            // So sánh key
            if (klen == key_len && strncmp(p, key, key_len) == 0) {
                if (vlen + 1 > out_cap)
                    return 0;
                // Copy value vào out
                memcpy(out, eq + 1, vlen);
                out[vlen] = 0;
                return 1;
            }
        }

        p = token_end ? token_end + 1 : p + token_len;
    }

    return 0;
}

// ============ Response Parser ============

/**
 * parse_response
 * Parse response line từ server: "OK req_id payload" hoặc "ERR req_id code msg"
 * @param line: Response line từ server
 * @param kind: Output "OK" hoặc "ERR"
 * @param rid: Output request ID (để match với request)
 * @param rest: Output phần còn lại (payload hoặc error message)
 */
int parse_response(const char *line,
                   char *kind, size_t kind_cap,
                   char *rid, size_t rid_cap,
                   char *rest, size_t rest_cap)
{
    (void)kind_cap;
    (void)rid_cap;

    if (!line)
        return -1;
    kind[0] = 0;
    rid[0] = 0;
    rest[0] = 0;

    const char *p = line;
    while (*p == ' ')
        p++;

    if (sscanf(p, "%31s %31s", kind, rid) != 2)
        return -1;

    const char *t = strstr(p, rid);
    if (!t)
        return 0;
    t += strlen(rid);
    while (*t == ' ')
        t++;

    strncpy(rest, t, rest_cap - 1);
    rest[rest_cap - 1] = 0;
    return 0;
}

// ============ Base64 ============

/**
 * base64_encode
 * Mã hóa binary data sang Base64 string
 * Dùng để encode nội dung tin nhắn (có thể chứa ký tự đặc biệt, Unicode)
 * @param src: Dữ liệu gốc
 * @param src_len: Độ dài dữ liệu gốc
 * @param out: Buffer output (phải đủ lớn: src_len * 4/3 + 4)
 * @return: Độ dài output nếu thành công, -1 nếu buffer không đủ
 */
int base64_encode(const unsigned char *src, size_t src_len,
                  char *out, size_t out_cap)
{
    // Tính độ dài output: mỗi 3 bytes input -> 4 bytes output
    size_t out_len = ((src_len + 2) / 3) * 4;
    if (out_len + 1 > out_cap)
        return -1;

    size_t i = 0, j = 0;
    // Xử lý từng nhóm 3 bytes
    while (i < src_len) {
        unsigned int a = i < src_len ? src[i++] : 0;
        unsigned int b = i < src_len ? src[i++] : 0;
        unsigned int c = i < src_len ? src[i++] : 0;

        // Ghép 3 bytes thành 24 bits, chia thành 4 nhóm 6 bits
        unsigned int triple = (a << 16) | (b << 8) | c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >> 6) & 0x3F];
        out[j++] = b64_table[triple & 0x3F];
    }

    // Thêm padding '=' nếu input không chia hết cho 3
    size_t mod = src_len % 3;
    if (mod == 1) {
        out[j - 1] = '=';
        out[j - 2] = '=';
    } else if (mod == 2) {
        out[j - 1] = '=';
    }

    out[j] = '\0';
    return (int)j;
}

/**
 * base64_decode
 * Giải mã Base64 string về binary data
 * @param src: Base64 string (độ dài phải chia hết cho 4)
 * @param out: Buffer output
 * @return: Độ dài output nếu thành công, -1 nếu lỗi
 */
int base64_decode(const char *src, unsigned char *out, size_t out_cap)
{
    size_t src_len = strlen(src);
    // Base64 luôn có độ dài chia hết cho 4
    if (src_len % 4 != 0)
        return -1;

    // Tính độ dài output: mỗi 4 chars -> 3 bytes (trừ padding)
    size_t out_len = (src_len / 4) * 3;
    if (src_len > 0 && src[src_len - 1] == '=')
        out_len--;
    if (src_len > 1 && src[src_len - 2] == '=')
        out_len--;

    if (out_len + 1 > out_cap)
        return -1;

    size_t i = 0, j = 0;
    // Xử lý từng nhóm 4 chars
    while (i < src_len) {
        // Tra bảng decode cho mỗi ký tự
        int a = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]];
        i++;
        int b = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]];
        i++;
        int c = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]];
        i++;
        int d = src[i] == '=' ? 0 : b64_decode_table[(unsigned char)src[i]];
        i++;

        // Kiểm tra ký tự không hợp lệ
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return -1;

        // Ghép 4 nhóm 6 bits thành 3 bytes
        unsigned int triple = (a << 18) | (b << 12) | (c << 6) | d;

        if (j < out_len)
            out[j++] = (triple >> 16) & 0xFF;
        if (j < out_len)
            out[j++] = (triple >> 8) & 0xFF;
        if (j < out_len)
            out[j++] = triple & 0xFF;
    }

    out[j] = '\0';
    return (int)out_len;
}
