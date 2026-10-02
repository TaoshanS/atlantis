/* sbso_dump: saves the main SWF of SpongeBob Atlantis SquareOff from the memory of the running game.
 *
 * The PC release wraps its main movie in sbso.exe (Zinc projector packed with Armadillo), so the SWF only exists decrypted in memory
 * while the game runs. Start the game, wait for the title screen, then run (same Windows user):
 *
 *     sbso_dump.exe [out.swf] [--wait SECONDS] [--all DIR]
 *
 *   out.swf        where to write it (default: sbso_main.swf in the current folder); copy it to game/sbso_main.swf
 *   --wait N       keep looking for up to N seconds (default 120), e.g. while the game is still loading
 *   --all DIR      also write every uncompressed AS3 SWF found to DIR (for other releases / troubleshooting)
 *
 * The main movie is recognised by its document class (sbso2) and checked against the SHA-256 of the known release. Windows XP or later,
 * or Wine. Build: i686-w64-mingw32-gcc -O2 -s -o sbso_dump.exe sbso_dump.c   (32-bit on purpose: the game is a 32-bit process)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kKnownSha256 = "0185333a129cc29712b1f7ba94c16ad6497325146490b87c679f686628515edc"; /* 2008 WildGames/Big Fish release */
static const char *kDocumentClass = "sbso2";
#define MAX_SWF (64u * 1024u * 1024u)
#define MAX_REGION (512u * 1024u * 1024u)

/* ---- SHA-256 (FIPS 180-4), small and dependency free ---- */
typedef struct { unsigned int h[8]; unsigned char buf[64]; unsigned long long len; unsigned int n; } sha256;
static const unsigned int K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(sha256 *s, const unsigned char *p) {
    unsigned int w[64], a, b, c, d, e, f, g, h, i, t1, t2;
    for (i = 0; i < 16; i++) w[i] = (unsigned int)p[i * 4] << 24 | (unsigned int)p[i * 4 + 1] << 16 | (unsigned int)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (i = 16; i < 64; i++)
        w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] + (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (i = 0; i < 64; i++) {
        t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha256_hex(const unsigned char *data, size_t len, char out[65]) {
    static const unsigned int H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    sha256 s;
    unsigned char tail[128];
    size_t i, rest;
    unsigned long long bits = (unsigned long long)len * 8;
    memcpy(s.h, H0, sizeof H0);
    for (i = 0; i + 64 <= len; i += 64) sha_block(&s, data + i);
    rest = len - i;
    memset(tail, 0, sizeof tail);
    memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    rest = rest + 1 + 8 <= 64 ? 64 : 128;
    for (i = 0; i < 8; i++) tail[rest - 1 - i] = (unsigned char)(bits >> (8 * i));
    sha_block(&s, tail);
    if (rest == 128) sha_block(&s, tail + 64);
    for (i = 0; i < 8; i++) sprintf(out + i * 8, "%08x", s.h[i]);
}

/* ---- memory scan ---- */
static int contains(const unsigned char *p, size_t n, const char *needle) {
    size_t m = strlen(needle), i;
    for (i = 0; i + m <= n; i++)
        if (p[i] == (unsigned char)needle[0] && memcmp(p + i, needle, m) == 0) return 1;
    return 0;
}

static int write_file(const char *path, const unsigned char *p, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  cannot write %s\n", path); return 0; }
    fwrite(p, 1, n, f);
    fclose(f);
    return 1;
}

/* Returns 2 when the known main SWF was written, 1 when a probable main SWF (right class, unknown hash) was written, 0 otherwise. */
static int scan_process(DWORD pid, const char *out, const char *all_dir, int *n_all) {
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    SYSTEM_INFO si;
    unsigned char *addr, *region = NULL;
    size_t region_cap = 0;
    int result = 0;
    if (!h) { printf("  pid %lu: cannot open the process (error %lu); run as the same user / as administrator\n", pid, GetLastError()); return 0; }
    GetSystemInfo(&si);
    for (addr = (unsigned char *)si.lpMinimumApplicationAddress; addr < (unsigned char *)si.lpMaximumApplicationAddress;) {
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T got = 0;
        size_t i;
        if (VirtualQueryEx(h, addr, &mbi, sizeof mbi) != sizeof mbi) break;
        addr = (unsigned char *)mbi.BaseAddress + mbi.RegionSize;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || mbi.RegionSize > MAX_REGION) continue;
        if (mbi.RegionSize > region_cap) {
            free(region);
            region_cap = mbi.RegionSize;
            region = (unsigned char *)malloc(region_cap);
            if (!region) { region_cap = 0; continue; }
        }
        if (!ReadProcessMemory(h, mbi.BaseAddress, region, mbi.RegionSize, &got) || got < 8) continue;
        for (i = 0; i + 8 <= got; i++) {
            unsigned int len;
            unsigned char *swf;
            SIZE_T got2 = 0;
            char hex[65];
            int is_main;
            if (region[i] != 'F' || region[i + 1] != 'W' || region[i + 2] != 'S' || region[i + 3] < 9 || region[i + 3] > 40) continue;
            len = region[i + 4] | region[i + 5] << 8 | region[i + 6] << 16 | (unsigned int)region[i + 7] << 24;
            if (len < 64 || len > MAX_SWF) continue;
            swf = (unsigned char *)malloc(len);
            if (!swf) continue;
            /* the movie may continue past this region: read it straight from the process */
            if (!ReadProcessMemory(h, (unsigned char *)mbi.BaseAddress + i, swf, len, &got2) || got2 != len) { free(swf); continue; }
            sha256_hex(swf, len, hex);
            is_main = contains(swf, len, kDocumentClass);
            if (all_dir) {
                char path[MAX_PATH];
                _snprintf(path, sizeof path, "%s\\pid%lu_%08lx_%u.swf", all_dir, pid, (unsigned long)((ULONG_PTR)mbi.BaseAddress + i), len);
                path[sizeof path - 1] = 0;
                if (write_file(path, swf, len)) ++*n_all;
            }
            if (strcmp(hex, kKnownSha256) == 0) {
                printf("  found the main SWF (%u bytes, known release) in pid %lu\n", len, pid);
                if (write_file(out, swf, len)) result = 2;
            } else if (is_main && result == 0) {
                printf("  found a probable main SWF (%u bytes, class %s, unknown sha256 %.16s...) in pid %lu\n", len, kDocumentClass, hex, pid);
                if (write_file(out, swf, len)) result = 1;
            }
            free(swf);
            if (result == 2 && !all_dir) break;
        }
        if (result == 2 && !all_dir) break;
    }
    free(region);
    CloseHandle(h);
    return result;
}

static int scan_all(const char *out, const char *all_dir, int *n_procs) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    int best = 0, n_all = 0;
    *n_procs = 0;
    if (snap == INVALID_HANDLE_VALUE) return 0;
    pe.dwSize = sizeof pe;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "sbso.exe") == 0) {
                int r;
                ++*n_procs;
                r = scan_process(pe.th32ProcessID, out, all_dir, &n_all);
                if (r > best) best = r;
            }
        } while (best < 2 && Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    if (all_dir) printf("  %d SWF files written to %s\n", n_all, all_dir);
    return best;
}

int main(int argc, char **argv) {
    const char *out = "sbso_main.swf", *all_dir = NULL;
    int wait = 120, i, waited = 0, procs = 0, r = 0;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wait") == 0 && i + 1 < argc) wait = atoi(argv[++i]);
        else if (strcmp(argv[i], "--all") == 0 && i + 1 < argc) { all_dir = argv[++i]; CreateDirectoryA(all_dir, NULL); }
        else if (argv[i][0] == '-') { printf("usage: sbso_dump [out.swf] [--wait SECONDS] [--all DIR]\n"); return 2; }
        else out = argv[i];
    }
    printf("sbso_dump: looking for the main SWF in the memory of sbso.exe (start the game and wait for the title screen)\n");
    for (;;) {
        r = scan_all(out, all_dir, &procs);
        if (r > 0 || waited >= wait) break;
        if (procs == 0) printf("  sbso.exe is not running yet...\n");
        else printf("  not loaded yet, retrying...\n");
        Sleep(5000);
        waited += 5;
    }
    if (r == 2) { printf("Saved %s. Copy it to game/sbso_main.swf in the port's folder.\n", out); return 0; }
    if (r == 1) { printf("Saved %s, but it is not the known release: it may still work. Copy it to game/sbso_main.swf.\n", out); return 0; }
    printf("The main SWF was not found. Make sure the game is at its title screen, then try again (or use --all DIR and report).\n");
    return 1;
}
