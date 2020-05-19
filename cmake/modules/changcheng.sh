#!/bin/bash

echo "Kernel info:"
cat /proc/version
cat /proc/cmdline

echo "cpu info:"
cat /proc/cpuinfo

echo "os info:"
cat /etc/os-release
dmesg

echo "run dmidecode"
sudo dmidecode

cat > /tmp/source.c << 'EOF'
#include <stdio.h>
#include <stdint.h>

int spec_arch_intel_pclmul = 0;
int spec_arch_intel_sse42 = 0;
int spec_arch_intel_sse41 = 0;
int spec_arch_intel_ssse3 = 0;
int spec_arch_intel_sse3 = 0;
int spec_arch_intel_sse2 = 0;
int spec_arch_intel_aesni = 0;
int spec_arch_intel_avx = 0;
int spec_arch_intel_avx2 = 0;
int spec_arch_intel_avx512f = 0;
int spec_arch_intel_avx512er = 0;
int spec_arch_intel_avx512pf = 0;
int spec_arch_intel_avx512vl = 0;
int spec_arch_intel_avx512cd = 0;
int spec_arch_intel_avx512dq = 0;
int spec_arch_intel_avx512bw = 0;

#include <cpuid.h>

#ifndef _XCR_XFEATURE_ENABLED_MASK
#define _XCR_XFEATURE_ENABLED_MASK 0
#endif

#ifndef _XCR_XMM_YMM_STATE_ENABLED_BY_OS
#define _XCR_XMM_YMM_STATE_ENABLED_BY_OS 0x6
#endif

#ifndef _XCR_XMM_YMM_ZMM_STATE_ENABLED_BY_OS
#define _XCR_XMM_YMM_ZMM_STATE_ENABLED_BY_OS \
        ((0x3 << 5) | 0x6)
#endif

static inline int64_t _xgetbv(uint32_t index) {
    uint32_t eax, edx;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
}

static void detect_avx(void) {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    uint32_t max_level = __get_cpuid_max(0, NULL);
    if (max_level == 0) {
        return;
    }
    __cpuid_count(1, 0, eax, ebx, ecx, edx);
    if ((ecx & bit_OSXSAVE) == 0 || (ecx & bit_AVX) == 0) {
        return;
    }

    int64_t xcr_mask = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
    if (xcr_mask & _XCR_XMM_YMM_STATE_ENABLED_BY_OS) {
        spec_arch_intel_avx = 1;
    }
}

static void detect_avx2(void) {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    uint32_t max_level = __get_cpuid_max(0, NULL);
    if (max_level == 0) {
        return;
    }
    __cpuid_count(1, 0, eax, ebx, ecx, edx);
    printf("avx2: ecx:0x%x\n", ecx);
    if ((ecx & bit_OSXSAVE) == 0 || (ecx & bit_AVX) == 0) {
        return;
    }
    if (max_level < 7) {
        return;
    }
    __cpuid_count(7, 0, eax, ebx, ecx, edx);
    printf("avx2: ebx:0x%x\n", ebx);
    if ((ebx & bit_AVX2) == 0) {
        return;
    }
    int64_t xcr_mask = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
    printf("avx2: xcr_mask:0x%lx\n", xcr_mask);
    if (xcr_mask & _XCR_XMM_YMM_STATE_ENABLED_BY_OS) {
        spec_arch_intel_avx2 = 1;
    }
}

static void detect_avx512(void) {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    uint32_t max_level = __get_cpuid_max(0, NULL);
    if (max_level == 0) {
        return;
    }
    __cpuid_count(1, 0, eax, ebx, ecx, edx);
    if ((ecx & bit_OSXSAVE) == 0) {
        return;
    }
    int64_t xcr_mask = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
    if ((xcr_mask & _XCR_XMM_YMM_ZMM_STATE_ENABLED_BY_OS) == 0) {
        return;
    }
    if (max_level < 7) {
        return;
    }
    __cpuid_count(7, 0, eax, ebx, ecx, edx);
    if ((ebx & bit_AVX512F) == 0) {
        return;
    }
    spec_arch_intel_avx512f = 1;

    if(ebx & bit_AVX512ER) {
        spec_arch_intel_avx512er = 1;
    }
    if(ebx & bit_AVX512PF) {
        spec_arch_intel_avx512pf = 1;
    }

    if ((ebx & bit_AVX512VL)== 0) {
        return;
    }
    spec_arch_intel_avx512vl = 1;

    if(ebx & bit_AVX512CD) {
        spec_arch_intel_avx512cd = 1;
    }
    if(ebx & bit_AVX512DQ) {
        spec_arch_intel_avx512dq = 1;
    }
    if (ebx & bit_AVX512BW) {
        spec_arch_intel_avx512bw = 1;
    }
}

int spec_arch_intel_probe(void)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    uint32_t max_level = __get_cpuid_max(0, NULL);
    if (max_level == 0) {
        return 0;
    }
    __cpuid_count(1, 0, eax, ebx, ecx, edx);

    if ((ecx & bit_PCLMUL) != 0) {
        spec_arch_intel_pclmul = 1;
    }
    if ((ecx & bit_SSE4_2) != 0) {
        spec_arch_intel_sse42 = 1;
    }
    if ((ecx & bit_SSE4_1) != 0) {
        spec_arch_intel_sse41 = 1;
    }
    if ((ecx & bit_SSSE3) != 0) {
        spec_arch_intel_ssse3 = 1;
    }
    if ((ecx & bit_SSE3) != 0) {
        spec_arch_intel_sse3 = 1;
    }
    if ((edx & bit_SSE2) != 0) {
        spec_arch_intel_sse2 = 1;
    }
    if ((ecx & bit_AES) != 0) {
        spec_arch_intel_aesni = 1;
    }

    detect_avx();
    detect_avx2();
    detect_avx512();

    return 0;
}

int main(void) {
    spec_arch_intel_probe();
    printf("pclmul: %d\n", spec_arch_intel_pclmul);
    printf("sse42: %d\n", spec_arch_intel_sse42);
    printf("sse41: %d\n", spec_arch_intel_sse41);
    printf("ssse3: %d\n", spec_arch_intel_ssse3);
    printf("sse3: %d\n", spec_arch_intel_sse3);
    printf("sse2: %d\n", spec_arch_intel_sse2);
    printf("aesni: %d\n", spec_arch_intel_aesni);
    printf("avx: %d\n", spec_arch_intel_avx);
    printf("avx2: %d\n", spec_arch_intel_avx2);
    printf("avx512f: %d\n", spec_arch_intel_avx512f);
    printf("avx512er: %d\n", spec_arch_intel_avx512er);
    printf("avx512pf: %d\n", spec_arch_intel_avx512pf);
    printf("avx512vl: %d\n", spec_arch_intel_avx512vl);
    printf("avx512cd: %d\n", spec_arch_intel_avx512cd);
    printf("avx512dq: %d\n", spec_arch_intel_avx512dq);
    printf("avx512bw: %d\n", spec_arch_intel_avx512bw);
    return 0;
}

EOF

gcc -std=c99 /tmp/source.c -o /tmp/check_info
chmod +x /tmp/check_info
/tmp/check_info
