/* Minimal PNG writer: 8-bit RGB, zlib with stored (uncompressed) blocks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"

static uint32_t s_crc_table[256];

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    if (!s_crc_table[1]) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            s_crc_table[n] = c;
        }
    }
    for (size_t i = 0; i < len; i++) {
        crc = s_crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t head[8];
    put32(head, len);
    memcpy(head + 4, type, 4);
    fwrite(head, 1, 8, f);
    fwrite(data, 1, len, f);
    uint32_t crc = crc32_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
    crc = crc32_update(crc, data, len) ^ 0xFFFFFFFFu;
    uint8_t tail[4];
    put32(tail, crc);
    fwrite(tail, 1, 4, f);
}

bool sim_png_write(const char *path, const uint16_t *pixels, int width, int height, int zoom, bool dark)
{
    int w = width * zoom, h = height * zoom;
    size_t row = 1 + (size_t)w * 3;
    size_t raw_len = row * h;
    uint8_t *raw = malloc(raw_len);
    if (!raw) {
        return false;
    }
    for (int y = 0; y < h; y++) {
        uint8_t *out = raw + y * row;
        *out++ = 0;
        for (int x = 0; x < w; x++) {
            uint16_t px = dark ? 0 : pixels[(y / zoom) * width + x / zoom];
            uint8_t r = (px >> 11) & 0x1F, g = (px >> 5) & 0x3F, b = px & 0x1F;
            *out++ = (r << 3) | (r >> 2);
            *out++ = (g << 2) | (g >> 4);
            *out++ = (b << 3) | (b >> 2);
        }
    }

    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len);
    if (!z) {
        free(raw);
        return false;
    }
    uint8_t *p = z;
    *p++ = 0x78;
    *p++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *p++ = off + n == raw_len;
        *p++ = n & 0xFF;
        *p++ = n >> 8;
        *p++ = ~n & 0xFF;
        *p++ = (~n >> 8) & 0xFF;
        memcpy(p, raw + off, n);
        p += n;
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[off + i]) % 65521;
            b = (b + a) % 65521;
        }
    }
    put32(p, (b << 16) | a);
    p += 4;

    FILE *f = fopen(path, "wb");
    bool ok = f != NULL;
    if (ok) {
        static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        fwrite(sig, 1, 8, f);
        uint8_t ihdr[13] = { 0 };
        put32(ihdr, w);
        put32(ihdr + 4, h);
        ihdr[8] = 8;
        ihdr[9] = 2;
        chunk(f, "IHDR", ihdr, sizeof(ihdr));
        chunk(f, "IDAT", z, (uint32_t)(p - z));
        chunk(f, "IEND", NULL, 0);
        ok = fclose(f) == 0;
    }
    free(z);
    free(raw);
    return ok;
}
