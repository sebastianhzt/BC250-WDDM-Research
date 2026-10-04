/* SPDX-License-Identifier: Apache-2.0 */
#include "bc250_pnp_capture.h"
#ifdef BC250_PNP_CAPTURE_MOCK
extern BOOLEAN Bc250CaptureMockAllowed;
#endif
static BOOLEAN CaptureEnabled(VOID)
{
#ifdef BC250_PNP_CAPTURE_MOCK
    return Bc250CaptureMockAllowed;
#else
    return FALSE;
#endif
}
static NTSTATUS CapturePolicy(VOID)
{
    if(!CaptureEnabled())return STATUS_NOT_SUPPORTED;
    return KeGetCurrentIrql()==PASSIVE_LEVEL ? STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
}
static NTSTATUS CaptureGuard(BC250_CAPTURE_CACHE *Cache)
{
    NTSTATUS status=CapturePolicy();
    if(status!=STATUS_SUCCESS)return status;
    return Cache!=NULL && Cache->Signature==BC250_CAPTURE_SIGNATURE ?
        STATUS_SUCCESS:STATUS_INVALID_PARAMETER;
}
static ULONG Wire32(const UCHAR *p)
{
    return (ULONG)p[0]|((ULONG)p[1]<<8)|((ULONG)p[2]<<16)|((ULONG)p[3]<<24);
}
static ULONGLONG Wire64(const UCHAR *p)
{
    return (ULONGLONG)Wire32(p)|((ULONGLONG)Wire32(p+4)<<32);
}
static BOOLEAN CaptureZero(const UCHAR *p,ULONG bytes)
{
    ULONG n;
    for(n=0;n<bytes;++n)if(p[n])return FALSE;
    return TRUE;
}
/* Fixed array input, exact v2/360-byte ABI; no variable-length pointer walk. */
static NTSTATUS DecodeResources(const BC250_CAPTURE_INPUT *Input,BC250_PCI_RESOURCES *Output)
{
    const UCHAR *w=Input->Wire;
    ULONG snapshot=Wire32(w+BC250_CW_SNAPSHOT),count=Wire32(w+BC250_CW_COUNT);
    ULONG descriptors=Wire32(w+BC250_CW_DESCRIPTORS),blockers=Wire32(w+BC250_CW_BLOCKERS),n,k;
    if(Input->Version!=BC250_CAPTURE_SCHEMA || Input->StructSize!=sizeof(*Input) ||
        Input->SourceGeneration==0 || Input->SourceGeneration>=BC250_CAPTURE_COUNTER_LIMIT ||
        Input->SourceEpoch>=BC250_CAPTURE_COUNTER_LIMIT || Input->PowerState>4 ||
        Input->InterlocksOff>1 ||
        Wire32(w+BC250_CW_VERSION)!=BC250_CAPTURE_WIRE_VERSION ||
        Wire32(w+BC250_CW_SIZE)!=BC250_CAPTURE_WIRE_BYTES ||
        Wire32(w+BC250_CW_BUILD)!=BC250_CAPTURE_WIRE_BUILD ||
        Wire32(w+BC250_CW_STATUS)!=(ULONG)STATUS_DEVICE_NOT_READY ||
        Wire32(w+BC250_CW_GENERATION)!=Input->SourceGeneration ||
        Wire32(w+BC250_CW_STATE)>6 || (Wire32(w+BC250_CW_STATE)==1 && Input->SourceEpoch==0) ||
        (blockers&0x1cU)!=0x1cU || (blockers&~0x1fU)!=0 ||
        snapshot>2 || descriptors>32 || count>BC250_PCI_MAX_MEMORY)
        return STATUS_DATA_ERROR;
    if(snapshot!=1) {
        if(count!=0 || !CaptureZero(w+BC250_CW_MEMORY,BC250_CAPTURE_WIRE_BYTES-BC250_CW_MEMORY) ||
            (snapshot==0 && (blockers&1U)==0) || (snapshot==2 && (blockers&2U)==0))
            return STATUS_DATA_ERROR;
        return STATUS_SUCCESS; /* Non-ready metadata retained, resources stay zero. */
    }
    if((blockers&3U)!=0 || descriptors==0 || count>descriptors)
        return STATUS_DATA_ERROR;
    Output->DescriptorCount=descriptors;Output->MemoryCount=count;
    Output->Started=Wire32(w+BC250_CW_STATE)==1;Output->Valid=1;
    for(n=0;n<count;++n) {
        const UCHAR *m=w+BC250_CW_MEMORY+n*BC250_CW_ENTRY_BYTES;
        BC250_PCI_MEMORY *v=&Output->Memory[n];
        ULONG rawFlags=Wire32(m+BC250_CW_RAW_FLAGS),translatedFlags=Wire32(m+BC250_CW_TRANSLATED_FLAGS);
        v->RawBase=Wire64(m+BC250_CW_RAW);v->TranslatedBase=Wire64(m+BC250_CW_TRANSLATED);
        v->Length=Wire64(m+BC250_CW_LENGTH);v->Ordinal=Wire32(m+BC250_CW_ORDINAL);
        if(rawFlags>0xffffU || translatedFlags>0xffffU || Wire32(m+BC250_CW_RESERVED)!=0 ||
            v->Ordinal>=descriptors || (n && v->Ordinal<=Output->Memory[n-1].Ordinal) ||
            v->RawBase==0 || v->Length==0 || v->Length>0x7fffffffffffffffULL ||
            v->RawBase>0x7fffffffffffffffULL-v->Length ||
            v->TranslatedBase>0x7fffffffffffffffULL-v->Length)
            return STATUS_DATA_ERROR;
        v->RawFlags=(USHORT)rawFlags;v->TranslatedFlags=(USHORT)translatedFlags;
        for(k=0;k<n;++k) {
            const BC250_PCI_MEMORY *a=&Output->Memory[k];
            if((v->RawBase<a->RawBase+a->Length && a->RawBase<v->RawBase+v->Length) ||
                (v->TranslatedBase<a->TranslatedBase+a->Length &&
                 a->TranslatedBase<v->TranslatedBase+v->Length))
                return STATUS_DATA_ERROR;
        }
    }
    if(!CaptureZero(w+BC250_CW_MEMORY+count*BC250_CW_ENTRY_BYTES,
        (BC250_PCI_MAX_MEMORY-count)*BC250_CW_ENTRY_BYTES))return STATUS_DATA_ERROR;
    return STATUS_SUCCESS;
}
static BOOLEAN CaptureReady(const BC250_CAPTURE_FRAME *Frame)
{
    return Wire32(Frame->Input.Wire+BC250_CW_STATE)==1 &&
        Frame->Input.PowerState==BC250_CAPTURE_D0 && Frame->Input.InterlocksOff==1 &&
        Frame->Resources.Valid==1 && Frame->Resources.Started==1 && Frame->Resources.MemoryCount!=0;
}
/* Caller holds Cache.Lock; token fields and all bytes must agree. */
static BOOLEAN CaptureSame(BC250_CAPTURE_CACHE *Cache,const BC250_CAPTURE_FRAME *Frame)
{
    return !Cache->Fault && Cache->Present && Frame!=NULL && Frame->Origin==Cache &&
        Frame->ScopeId==Cache->ScopeId && Frame->Revision==Cache->Revision &&
        RtlCompareMemory(Frame,&Cache->Current,sizeof(*Frame))==sizeof(*Frame) &&
        CaptureReady(&Cache->Current);
}
NTSTATUS Bc250CaptureInit(BC250_CAPTURE_CACHE *Cache,ULONGLONG ScopeId)
{
    BC250_CAPTURE_CACHE zero;
    NTSTATUS status=CapturePolicy();
    if(status!=STATUS_SUCCESS)return status;
    RtlZeroMemory(&zero,sizeof(zero));
    if(Cache==NULL || ScopeId==0 || ScopeId==~(ULONGLONG)0 ||
        RtlCompareMemory(Cache,&zero,sizeof(zero))!=sizeof(zero))return STATUS_INVALID_PARAMETER;
    Cache->ScopeId=ScopeId;KeInitializeSpinLock(&Cache->Lock);
    Cache->Signature=BC250_CAPTURE_SIGNATURE;return STATUS_SUCCESS;
}
NTSTATUS Bc250CaptureCommit(BC250_CAPTURE_CACHE *Cache,const BC250_CAPTURE_INPUT *Input)
{
    BC250_CAPTURE_FRAME scratch;
    KIRQL oldIrql;
    NTSTATUS status=CaptureGuard(Cache);
    if(status!=STATUS_SUCCESS)return status;
    RtlZeroMemory(&scratch,sizeof(scratch));
    status=Input==NULL ? STATUS_INVALID_PARAMETER:DecodeResources(Input,&scratch.Resources);
    if(status==STATUS_SUCCESS) {
        scratch.Input=*Input;scratch.Origin=Cache;scratch.ScopeId=Cache->ScopeId;
    }
    KeAcquireSpinLock(&Cache->Lock,&oldIrql);
    if(Cache->Fault || Cache->Revision>=~(ULONGLONG)0-1U)
        status=STATUS_INVALID_DEVICE_STATE;
    else if(status==STATUS_SUCCESS &&
        (Cache->Present && (Input->SourceGeneration<Cache->Current.Input.SourceGeneration ||
          (Input->SourceGeneration==Cache->Current.Input.SourceGeneration &&
           Input->SourceEpoch<=Cache->Current.Input.SourceEpoch))))
        status=STATUS_INVALID_DEVICE_STATE;
    if(status==STATUS_SUCCESS) {
        scratch.Revision=++Cache->Revision;
        scratch.Resources.Generation=scratch.Resources.Epoch=scratch.Revision;
        Cache->Current=scratch;Cache->Present=1;
    } else {
        Cache->Fault=1;Cache->Present=0;
        RtlZeroMemory(&Cache->Current,sizeof(Cache->Current));
    }
    KeReleaseSpinLock(&Cache->Lock,oldIrql);
    RtlZeroMemory(&scratch,sizeof(scratch));return status;
}
NTSTATUS Bc250CaptureRead(BC250_CAPTURE_CACHE *Cache,BC250_CAPTURE_FRAME *Output)
{
    KIRQL oldIrql;
    NTSTATUS status;
    if(Output!=NULL)RtlZeroMemory(Output,sizeof(*Output));
    status=CaptureGuard(Cache);
    if(status!=STATUS_SUCCESS)return status;
    if(Output==NULL)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Cache->Lock,&oldIrql);
    if(Cache->Fault || !Cache->Present || !CaptureReady(&Cache->Current))status=STATUS_DEVICE_NOT_READY;
    else *Output=Cache->Current;
    KeReleaseSpinLock(&Cache->Lock,oldIrql);return status;
}
NTSTATUS Bc250CaptureValidate(BC250_CAPTURE_CACHE *Cache,const BC250_CAPTURE_FRAME *Frame)
{
    KIRQL oldIrql;
    NTSTATUS status=CaptureGuard(Cache);
    if(status!=STATUS_SUCCESS)return status;
    if(Frame==NULL)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Cache->Lock,&oldIrql);
    if(!CaptureSame(Cache,Frame))status=STATUS_DEVICE_NOT_READY;
    KeReleaseSpinLock(&Cache->Lock,oldIrql);return status;
}
NTSTATUS Bc250CaptureResources(BC250_CAPTURE_CACHE *Cache,const BC250_CAPTURE_FRAME *Frame,
    BC250_PCI_RESOURCES *Output)
{
    KIRQL oldIrql;
    NTSTATUS status;
    if(Output!=NULL)RtlZeroMemory(Output,sizeof(*Output));
    status=CaptureGuard(Cache);
    if(status!=STATUS_SUCCESS)return status;
    if(Output==NULL || Frame==NULL)return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Cache->Lock,&oldIrql);
    if(!CaptureSame(Cache,Frame))status=STATUS_DEVICE_NOT_READY;
    else *Output=Cache->Current.Resources; /* Same locked validation and copy. */
    KeReleaseSpinLock(&Cache->Lock,oldIrql);return status;
}
