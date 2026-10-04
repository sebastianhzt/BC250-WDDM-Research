/* SPDX-License-Identifier: Apache-2.0
 * Mechanically extracted production functions with RAM-only fake Windows APIs.
 * No real firmware files, device handles, GPU, driver load or OS mapping APIs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wchar.h>
#include "upstream-integration-20261003/bc250_pm4_bounds.h"

static unsigned int checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #x); exit(1); } } while (0)
typedef void VOID;
typedef void *PVOID;
typedef unsigned long ULONG, *PULONG;
typedef unsigned long long ULONG64;
typedef unsigned char UCHAR, *PUCHAR;
typedef const wchar_t *PCWSTR;
typedef int NTSTATUS;
typedef void *HANDLE;
typedef struct { long long QuadPart; } PHYSICAL_ADDRESS;
typedef struct { size_t Information; } IO_STATUS_BLOCK;
typedef struct { PHYSICAL_ADDRESS EndOfFile; } FILE_STANDARD_INFORMATION;
typedef struct { unsigned int Unused; } OBJECT_ATTRIBUTES, UNICODE_STRING;
#ifndef _In_
#define _In_
#define _Out_
#endif
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER (-1)
#define STATUS_INVALID_DEVICE_STATE (-2)
#define STATUS_INVALID_BUFFER_SIZE (-3)
#define STATUS_INSUFFICIENT_RESOURCES (-4)
#define STATUS_END_OF_FILE (-5)
#define PASSIVE_LEVEL 0U
#define NT_SUCCESS(x) ((x) >= 0)
#include "firmware-defines.inc"
#define POOL_FLAG_NON_PAGED 1U
#define DPFLTR_IHVVIDEO_ID 0U
#define DPFLTR_INFO_LEVEL 0U
#define KdPrintEx(x) ((void)0)
#define RtlZeroMemory(p, n) memset(p, 0, n)
#define RtlInitUnicodeString(p, s) do { (void)(p); (void)(s); } while (0)
#define InitializeObjectAttributes(p, ...) ((void)(p))

static unsigned int fake_irql, opens, closes, allocations, releases, queries, reads;
static NTSTATUS open_status, query_status, read_status;
static long long file_bytes;
static size_t read_bytes;
static int fail_allocate;
static void *live;
static unsigned int KeGetCurrentIrql(void) { return fake_irql; }
static NTSTATUS FakeCreate(HANDLE *handle)
{
    ++opens;
    if (NT_SUCCESS(open_status)) *handle = (HANDLE)(uintptr_t)1;
    return open_status;
}
static NTSTATUS FakeQuery(HANDLE handle, IO_STATUS_BLOCK *io, FILE_STANDARD_INFORMATION *info)
{
    CHECK(handle == (HANDLE)(uintptr_t)1); (void)io;
    ++queries; info->EndOfFile.QuadPart = file_bytes; return query_status;
}
static NTSTATUS FakeRead(HANDLE handle, IO_STATUS_BLOCK *io, PUCHAR buffer, ULONG bytes)
{
    CHECK(handle == (HANDLE)(uintptr_t)1); CHECK(buffer == live); ++reads;
    memset(buffer, 0xD4, bytes); io->Information = read_bytes; return read_status;
}
static void ZwClose(HANDLE handle) { CHECK(handle == (HANDLE)(uintptr_t)1); ++closes; }
static void *ExAllocatePool2(unsigned int flags, ULONG bytes, unsigned int tag)
{
    (void)flags; (void)tag; ++allocations; CHECK(!live); CHECK(bytes && bytes <= MAX_FW_SIZE);
    if (fail_allocate) return NULL;
    live = malloc(bytes); CHECK(live != NULL); return live;
}
static void ExFreePoolWithTag(void *p, unsigned int tag)
{
    (void)tag; CHECK(p == live); CHECK(live != NULL); ++releases; free(p); live = NULL;
}
#define ZwCreateFile(handle, ...) FakeCreate(handle)
#define ZwQueryInformationFile(handle, io, info, ...) FakeQuery(handle, io, info)
#define ZwReadFile(handle, event, apc, ctx, io, buf, bytes, ...) FakeRead(handle, io, buf, bytes)
#include "firmware-under-test.inc"

typedef struct {
    struct { PVOID VirtualAddress; size_t SizeInBytes; ULONG WritePointer; } GfxRing;
    struct { PHYSICAL_ADDRESS PhysicalAddress; } GlobalFence;
} DREAM_V3_DEVICE_EXTENSION, *PDREAM_V3_DEVICE_EXTENSION;
#include "pm4-defines.inc"
#include "pm4-under-test.inc"

static void ResetLoader(void)
{
    CHECK(!live);
    fake_irql = opens = closes = allocations = releases = queries = reads = 0;
    open_status = query_status = read_status = 0;
    file_bytes = 64; read_bytes = 64; fail_allocate = 0;
}

static void LoaderRejected(void)
{
    PUCHAR data = (PUCHAR)(uintptr_t)0xA5;
    ULONG bytes = 0xA5;
    CHECK(!NT_SUCCESS(DreamV3LoadFirmwareFromFile(L"FAKE", &data, &bytes)));
    CHECK(data == NULL && bytes == 0); CHECK(!live);
}

static void TestLoader(void)
{
    PUCHAR data; ULONG bytes;
    const long long bad_sizes[] = {0, -1, MAX_FW_SIZE + 1LL, 0x100000040LL, INT64_MAX};
    unsigned int i;
    ResetLoader(); data = (PUCHAR)(uintptr_t)1; bytes = 7;
    CHECK(DreamV3LoadFirmwareFromFile(NULL, &data, &bytes) == STATUS_INVALID_PARAMETER);
    CHECK(data == NULL && bytes == 0 && opens == 0);
    data = (PUCHAR)(uintptr_t)1; bytes = 7;
    CHECK(DreamV3LoadFirmwareFromFile(L"FAKE", NULL, &bytes) == STATUS_INVALID_PARAMETER);
    CHECK(bytes == 0 && opens == 0);
    CHECK(DreamV3LoadFirmwareFromFile(L"FAKE", &data, NULL) == STATUS_INVALID_PARAMETER);
    CHECK(data == NULL && opens == 0);
    ResetLoader(); fake_irql = 1; LoaderRejected(); CHECK(opens == 0);
    ResetLoader(); open_status = -10; LoaderRejected(); CHECK(opens == 1 && closes == 0 && queries == 0);
    ResetLoader(); query_status = -11; LoaderRejected(); CHECK(closes == 1 && queries == 1 && allocations == 0);
    for (i = 0; i < sizeof(bad_sizes) / sizeof(bad_sizes[0]); ++i) {
        ResetLoader(); file_bytes = bad_sizes[i]; LoaderRejected();
        CHECK(closes == 1 && allocations == 0 && reads == 0);
    }
    ResetLoader(); fail_allocate = 1; LoaderRejected(); CHECK(closes == 1 && allocations == 1 && releases == 0);
    ResetLoader(); read_status = -12; LoaderRejected(); CHECK(closes == 1 && reads == 1 && releases == 1);
    for (i = 0; i < 3; ++i) {
        ResetLoader(); read_bytes = i == 0 ? 0 : (i == 1 ? 63 : 65);
        LoaderRejected(); CHECK(closes == 1 && reads == 1 && releases == 1);
    }
    for (i = 0; i < 3; ++i) {
        ResetLoader(); file_bytes = i == 0 ? 1 : (i == 1 ? 64 : MAX_FW_SIZE);
        read_bytes = (size_t)file_bytes;
        CHECK(DreamV3LoadFirmwareFromFile(L"FAKE", &data, &bytes) == STATUS_SUCCESS);
        CHECK(data == live && bytes == read_bytes && data[0] == 0xD4 && data[bytes - 1] == 0xD4);
        CHECK(closes == 1 && reads == 1 && releases == 0);
        ExFreePoolWithTag(data, 'fw'); CHECK(releases == 1 && !live);
    }
}

static void TestPlanner(void)
{
    unsigned int size, wptr, count;
    BC250_PM4_PLACEMENT plan, saved;
    memset(&saved, 0xA5, sizeof(saved));
    for (size = 0; size <= 128; ++size) {
        for (wptr = 0; wptr <= 132; ++wptr) {
            for (count = 0; count <= 34; ++count) {
                unsigned long long packet = ((unsigned long long)count + 1ULL) * 4ULL;
                int valid = size >= 8 && !(size % 4) && !(wptr % 4) &&
                    wptr <= size && count && packet <= size;
                plan = saved;
                CHECK(Bc250Pm4Plan(size, wptr, count, &plan) == valid);
                if (valid) {
                    unsigned int start = packet + wptr > size ? 0 : wptr;
                    CHECK(plan.PacketBytes == packet && plan.Start == start);
                    CHECK(plan.Next == start + packet && plan.Next <= size);
                    CHECK(plan.PaddingBytes == (start != wptr ? size - wptr : 0));
                } else CHECK(memcmp(&plan, &saved, sizeof(plan)) == 0);
            }
        }
    }
    plan = saved;
    CHECK(!Bc250Pm4Plan(1ULL << 32, 0, 1, &plan));
    CHECK(!Bc250Pm4Plan(0xFFFFFFFCULL, 0, UINT32_MAX, &plan));
    CHECK(!Bc250Pm4Plan(0xFFFFFFFCULL, 0, 16385, &plan));
    CHECK(Bc250Pm4Plan(0xFFFFFFFCULL, 0xFFFFFFFCU, 16384, &plan));
    CHECK(plan.Next == 65540U && plan.Start == 0 && plan.PaddingBytes == 0);
    CHECK(!Bc250Pm4Plan(64, 0, 1, NULL));
}

static void TestWriters(void)
{
    ULONG storage[18], before[18], values[16];
    DREAM_V3_DEVICE_EXTENSION dev;
    unsigned int type, wptr, count, i;
    memset(&dev, 0, sizeof(dev));
    for (i = 0; i < 16; ++i) values[i] = 0xC000U + i;
    for (type = 0; type < 3; ++type) {
        for (wptr = 0; wptr <= 68; ++wptr) {
            for (count = 0; count <= 17; ++count) {
                unsigned int n = type == 2 ? 5 : count;
                unsigned int packet = (n + 1U) * 4U;
                int valid = !(wptr % 4) && wptr <= 64 && n && packet <= 64;
                memset(storage, 0xA5, sizeof(storage)); memcpy(before, storage, sizeof(storage));
                dev.GfxRing.VirtualAddress = storage + 1; dev.GfxRing.SizeInBytes = 64; dev.GfxRing.WritePointer = wptr;
                dev.GlobalFence.PhysicalAddress.QuadPart = 0x56789000;
                if (type == 0) DreamV3WritePm4Type0(&dev, 0x100, values, count);
                if (type == 1) DreamV3WritePm4Type3(&dev, 0x10, values, count);
                if (type == 2) DreamV3WriteEopFence(&dev, 0x12345678ABCDEF01ULL);
                CHECK(storage[0] == before[0] && storage[17] == before[17]);
                if (!valid) {
                    CHECK(dev.GfxRing.WritePointer == wptr);
                    CHECK(memcmp(storage, before, sizeof(storage)) == 0);
                } else {
                    unsigned int start = wptr + packet > 64 ? 0 : wptr;
                    CHECK(dev.GfxRing.WritePointer == start + packet);
                    if (type < 2) {
                        for (i = 0; i < n; ++i) CHECK(storage[1 + start / 4 + i + 1] == values[i]);
                    } else {
                        CHECK(storage[1 + start / 4 + 4] == 0xABCDEF01UL);
                        CHECK(storage[1 + start / 4 + 5] == 0x12345678UL);
                    }
                    if (start != wptr) {
                        /* The wrapped packet can overwrite its own prior tail
                         * padding. This is bounds testing, NOT a free-space or
                         * hardware consumer/producer protocol acceptance test. */
                        for (i = wptr / 4; i < 16; ++i) {
                            if (i >= packet / 4) CHECK(storage[1 + i] == 0x80000000UL);
                        }
                    }
                }
            }
        }
    }
    DreamV3WritePm4Type0(NULL, 0, values, 1);
    DreamV3WritePm4Type3(NULL, 0, values, 1);
    DreamV3WriteEopFence(NULL, 0);
    memset(storage, 0xA5, sizeof(storage)); memcpy(before, storage, sizeof(storage));
    dev.GfxRing.SizeInBytes = 64; dev.GfxRing.WritePointer = 0;
    DreamV3WritePm4Type0(&dev, 0, NULL, 1);
    DreamV3WritePm4Type3(&dev, 0, NULL, 1);
    dev.GfxRing.SizeInBytes = 1ULL << 32;
    DreamV3WritePm4Type0(&dev, 0, values, UINT32_MAX);
    DreamV3WritePm4Type3(&dev, 0, values, UINT32_MAX);
    DreamV3WriteEopFence(&dev, 0);
    CHECK(dev.GfxRing.WritePointer == 0 && memcmp(storage, before, sizeof(storage)) == 0);
    /* Exercise the actual writers, not only their planner, at hostile inputs. */
    {
        const ULONG bad_counts[] = { 0U, 16385U, UINT32_MAX };
        const ULONG64 bad_sizes[] = { 0ULL, 7ULL, 63ULL, 1ULL << 32 };
        dev.GfxRing.SizeInBytes = 64;
        for (i = 0; i < sizeof(bad_counts) / sizeof(bad_counts[0]); ++i) {
            DreamV3WritePm4Type0(&dev, 0, values, bad_counts[i]);
            DreamV3WritePm4Type3(&dev, 0, values, bad_counts[i]);
            CHECK(dev.GfxRing.WritePointer == 0 && memcmp(storage, before, sizeof(storage)) == 0);
        }
        for (i = 0; i < sizeof(bad_sizes) / sizeof(bad_sizes[0]); ++i) {
            dev.GfxRing.SizeInBytes = bad_sizes[i];
            DreamV3WritePm4Type0(&dev, 0, values, 1);
            DreamV3WritePm4Type3(&dev, 0, values, 1);
            DreamV3WriteEopFence(&dev, 0);
            CHECK(dev.GfxRing.WritePointer == 0 && memcmp(storage, before, sizeof(storage)) == 0);
        }
        dev.GfxRing.SizeInBytes = 64;
        dev.GfxRing.VirtualAddress = NULL;
        DreamV3WritePm4Type0(&dev, 0, values, 1);
        DreamV3WritePm4Type3(&dev, 0, values, 1);
        DreamV3WriteEopFence(&dev, 0);
        CHECK(dev.GfxRing.WritePointer == 0 && memcmp(storage, before, sizeof(storage)) == 0);
    }
    /* Maximum accepted payload, including the write pointer exactly at end. */
    {
        ULONG *large = (ULONG *)malloc(16387U * sizeof(ULONG));
        ULONG *payload = (ULONG *)malloc(16384U * sizeof(ULONG));
        CHECK(large != NULL && payload != NULL);
        for (i = 0; i < 16384U; ++i) payload[i] = i ^ 0x5A5A0000UL;
        for (type = 0; type < 2; ++type) {
            memset(large, 0xA5, 16387U * sizeof(ULONG));
            dev.GfxRing.VirtualAddress = large + 1;
            dev.GfxRing.SizeInBytes = 65540U;
            dev.GfxRing.WritePointer = 65540U;
            if (type == 0) DreamV3WritePm4Type0(&dev, 0x100, payload, 16384U);
            else DreamV3WritePm4Type3(&dev, 0x10, payload, 16384U);
            CHECK(dev.GfxRing.WritePointer == 65540U);
            CHECK(large[0] == 0xA5A5A5A5UL && large[16386U] == 0xA5A5A5A5UL);
            CHECK(large[1] == (type == 0 ? PM4_TYPE0_HDR(0x100, 16384U) : PM4_TYPE3_HDR(0x10, 16384U)));
            CHECK(memcmp(large + 2, payload, 16384U * sizeof(ULONG)) == 0);
        }
        free(payload); free(large);
    }
}

int main(void)
{
    TestLoader(); TestPlanner(); TestWriters();
    printf("PASS: %u firmware/bounds/extracted-writer RAM assertions; no hardware\n", checks);
    return 0;
}
