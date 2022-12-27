#include "base/co_buffer.h"
#include "base/co_log.h"


namespace coserver
{

#ifndef UINTPTR_MAX
#if __LP64__
#define UINTPTR_MAX       18446744073709551615UL
#else
#define UINTPTR_MAX       4294967295UL
#endif
#endif

#define BUFFER_SIZE_MAX   UINTPTR_MAX


CoBuffer::CoBuffer()
{
}

CoBuffer::~CoBuffer() 
{
    SAFE_FREE(m_originBuffer);
}

int32_t CoBuffer::buffer_append(const char* data, size_t dataLen)
{
    size_t bufferUsed = m_misalignSize + m_bufferSize;

    if (m_totalSize - bufferUsed < dataLen) {
        // 缓冲区不足 申请新空间
        if (buffer_expand(dataLen) != CO_OK) {
            CO_SERVER_LOG_ERROR("appendbuffer expand failed, datalen:%lu", dataLen);
            return CO_ERROR;
        }
    }

    // 添加数据到缓冲区
    memcpy(m_buffer + m_bufferSize, data, dataLen);
    m_bufferSize += dataLen;
    return CO_OK;
}

int32_t CoBuffer::buffer_erase(size_t dataLen)
{
    if (dataLen >= m_bufferSize) {
        // 删除全部有效数据 重置相关标志位
        m_buffer = m_originBuffer;
        m_bufferSize = 0;
        m_misalignSize = 0;

    } else {
        // 删除部分数据  将有效数据起始地址向后移
        m_buffer += dataLen;
        m_bufferSize -= dataLen;
        m_misalignSize += dataLen;
    }

    return CO_OK;
}

int32_t CoBuffer::buffer_expand(size_t dataLen)
{
    size_t usedSize = m_misalignSize + m_bufferSize;
    if (m_totalSize < usedSize) {
        CO_SERVER_LOG_FATAL("buffer totalsize:%lu < usedsize:%lu,%lu", m_totalSize, m_misalignSize, m_bufferSize);
        return CO_ERROR;
    }

    if (m_totalSize - usedSize >= dataLen) {
        // 缓冲区空间充足 无需扩充
        return CO_OK;
    }

    // 缓冲区数据长度不合法
    if (usedSize + dataLen > BUFFER_SIZE_MAX) {
        CO_SERVER_LOG_ERROR("buffer size to large, usedsize:%lu,%lu datalen:%lu", m_misalignSize, m_bufferSize, dataLen);
        return CO_ERROR;
    }

    // 如果计算上m_uMisalignLen 判断缓冲区空间是否充足
    if (m_totalSize - m_bufferSize >= dataLen) {
        // 将有效数据移动到缓冲区头部 重置相关变量
        memmove(m_originBuffer, m_buffer, m_bufferSize);
        m_buffer = m_originBuffer;
        m_misalignSize = 0;
        return CO_OK;
    }

    // 需要额外申请空间 存储数据
    size_t reallocSize = m_totalSize;         // 一会需要重新申请的空间大小
    /*
        进行本次空间扩充需要的大小
        这里不能减去m_uMisalignSize错位数据长度  如果m_uMisalignSize过大 减去后needSize过小导致reallocSize无法正常增加
    */
    size_t needSize = m_bufferSize + dataLen;

    // 计算需要申请的空间大小
    if (reallocSize < 256) {
        reallocSize = 256;
    }
    if (reallocSize < BUFFER_SIZE_MAX / 2) {
        while (reallocSize < needSize) {
            reallocSize <<= 1;
        }
    } else {
        // 缓冲区空间已经非常大了 不再2倍申请空间
        reallocSize = needSize;
    }
    
    // 存在错位数据  将有效数据移动到缓冲区头部 进行空间利用
    if (m_originBuffer != m_buffer) {
        memmove(m_originBuffer, m_buffer, m_bufferSize);
        m_buffer = m_originBuffer;
        m_misalignSize = 0;
    }

    // 申请新缓冲区空间
    unsigned char* newBuffer = (unsigned char* )realloc(m_buffer, reallocSize);
    if (newBuffer == NULL) {
        CO_SERVER_LOG_FATAL("buffer realloc size:%lu failed", reallocSize);
        return CO_ERROR;
    }

    m_originBuffer = m_buffer = newBuffer;
    m_totalSize = reallocSize; 
    return CO_OK;
}

int32_t CoBuffer::buffer_size_expand(size_t dataLen)
{
    // 常规检查
    if (m_totalSize < m_misalignSize + m_bufferSize + dataLen) {
        CO_SERVER_LOG_ERROR("buffer space not enough, totalsize:%lu < misalignsize:%lu + buffersize:%lu + datalen:%lu", m_totalSize, m_misalignSize, m_bufferSize, dataLen);
        return CO_ERROR;
    }

    m_bufferSize += dataLen;
    return CO_OK;
}

void CoBuffer::reset() 
{
    m_buffer = m_originBuffer;
    m_bufferSize = m_misalignSize = 0;
}

}

