#include "RingBuffer.h"

#define RING_BUFFER_TAG	'uBiR'

RingBuffer::RingBuffer() 
	: m_Buffer(NULL), m_BufferLength(0), 
	m_LinearBufferReadPosition(0), m_LinearBufferWritePosition(0), m_IsFilling(TRUE)
{
}


RingBuffer::~RingBuffer()
{
	if (m_Buffer != NULL)
	{
		KeAcquireSpinLock(m_BufferLock, &m_SpinLockIrql);
		ExFreePoolWithTag(m_Buffer, RING_BUFFER_TAG);
		m_Buffer = NULL;
		m_BufferLength = 0;
		KeReleaseSpinLock(m_BufferLock, m_SpinLockIrql);
	}

	if (m_BufferLock != NULL) 
	{
		ExFreePoolWithTag(m_BufferLock, RING_BUFFER_TAG);
		m_BufferLock = NULL;
	}
}

NTSTATUS RingBuffer::Init(SIZE_T bufferSize)
{
	if (m_BufferLock == NULL)
	{
		m_BufferLock = new(NonPagedPoolNx, sizeof(KSPIN_LOCK)) KSPIN_LOCK;
		if (m_BufferLock == NULL) return STATUS_INSUFFICIENT_RESOURCES;
		KeInitializeSpinLock(m_BufferLock);
	}

	if (m_Buffer != NULL) 
	{
		KeAcquireSpinLock(m_BufferLock, &m_SpinLockIrql);
		ExFreePoolWithTag(m_Buffer, RING_BUFFER_TAG);
		m_Buffer = NULL;
		KeReleaseSpinLock(m_BufferLock, m_SpinLockIrql);
	}
	
	KeAcquireSpinLock(m_BufferLock, &m_SpinLockIrql);
	m_Buffer = static_cast<BYTE*>(ExAllocatePoolWithTag(NonPagedPoolNx, bufferSize, RING_BUFFER_TAG));
	if (m_Buffer == NULL) 
	{
		KeReleaseSpinLock(m_BufferLock, m_SpinLockIrql);
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	m_BufferLength = bufferSize;
	m_LinearBufferWritePosition = 0;
	m_LinearBufferReadPosition = 0;
	m_IsFilling = TRUE;
	KeReleaseSpinLock(m_BufferLock, m_SpinLockIrql);

	DbgPrintEx(0, 0, "Hello Kernel");

	return STATUS_SUCCESS;
}

NTSTATUS RingBuffer::Put(BYTE* pBytes, SIZE_T count) 
{
	if (count > m_BufferLength) return STATUS_BUFFER_TOO_SMALL;
	if (count == 0) return STATUS_SUCCESS;

	NTSTATUS status = STATUS_SUCCESS;
	//buffer overrun
	if ((m_LinearBufferWritePosition + count) - m_LinearBufferReadPosition > m_BufferLength)
	{
		status = STATUS_BUFFER_OVERFLOW;
		m_LinearBufferReadPosition = (m_LinearBufferWritePosition + count) - m_BufferLength + 1;
	}

	SIZE_T bufferOffset = m_LinearBufferWritePosition % m_BufferLength;
	SIZE_T bytesWritten = 0;
	while (count > 0)
	{
		SIZE_T runWrite = min(count, m_BufferLength - bufferOffset);
		RtlCopyMemory(m_Buffer + bufferOffset, pBytes, runWrite);
		bufferOffset = (bufferOffset + runWrite) % m_BufferLength;
		count -= runWrite;
		bytesWritten += runWrite;
	}
	m_LinearBufferWritePosition += bytesWritten;

	if (m_IsFilling && (m_LinearBufferWritePosition - m_LinearBufferReadPosition) > (m_BufferLength / 2))
	{
		DPF(D_TERSE, ("RingBuffer filled with %u bytes.", (m_LinearBufferWritePosition - m_LinearBufferReadPosition)));
		m_IsFilling = false;
	}
	return status;
}

NTSTATUS RingBuffer::Take(BYTE* pTarget, SIZE_T count, SIZE_T *readCount)
{
	KeAcquireSpinLock(m_BufferLock, &m_SpinLockIrql);

	if (m_IsFilling)
	{
		if (readCount != NULL)
		{
			*readCount = 0;
		}
		KeReleaseSpinLock(m_BufferLock, m_SpinLockIrql);
		return STATUS_DEVICE_NOT_READY;
	}

	count = min(count, m_LinearBufferWritePosition - m_LinearBufferReadPosition);
	SIZE_T bufferOffset = m_LinearBufferReadPosition % m_BufferLength;
	SIZE_T bytesRead = 0;
	while (count > 0)
	{
		SIZE_T runWrite = min(count, m_BufferLength - bufferOffset);
		RtlCopyMemory(pTarget + bytesRead, m_Buffer + bufferOffset, runWrite);
		bufferOffset = (bufferOffset + runWrite) % m_BufferLength;
		count -= runWrite;
		bytesRead += runWrite;
	}
	if (readCount != NULL)
	{
		*readCount = bytesRead;
	}
	m_LinearBufferReadPosition += bytesRead;
	if (m_LinearBufferWritePosition - m_LinearBufferReadPosition == 0)
	{
		DPF(D_TERSE, ("RingBuffer empty with %u bytes.", (m_LinearBufferWritePosition - m_LinearBufferReadPosition)));
		m_IsFilling = true;
	}

	KeReleaseSpinLock(m_BufferLock, m_SpinLockIrql);
	return STATUS_SUCCESS;
}



SIZE_T RingBuffer::GetSize()
{
	return m_BufferLength;
}

SIZE_T RingBuffer::GetAvailableBytes()
{
	return m_IsFilling ? 0 : m_LinearBufferWritePosition - m_LinearBufferReadPosition;
}

void RingBuffer::Clear()
{
	KeAcquireSpinLock(m_BufferLock, &m_SpinLockIrql);

	RtlZeroMemory(m_Buffer, m_BufferLength);
	m_IsFilling = true;
	m_LinearBufferReadPosition = 0;
	m_LinearBufferWritePosition = 0;

	KeReleaseSpinLock(m_BufferLock, m_SpinLockIrql);
}
