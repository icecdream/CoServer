#ifndef _CO_BUFFER_H_
#define _CO_BUFFER_H_

#include "base/co_common.h"


namespace coserver
{

const int32_t BUFFER_SIZE_4096 = 4096;

// 参考libevent evbuffer实现
class CoBuffer 
{
public:
    CoBuffer();
    ~CoBuffer();

public:
    int32_t buffer_append(const char* data, size_t uDataLen);   // 添加数据到缓冲区
    int32_t buffer_erase(size_t uDataLen);                      // 从缓冲区删除数据

    // 下面函数配合实现 先申请空间,写入数据,在增加有效数据长度 
    int32_t buffer_expand(size_t uDataLen);         // 缓冲区扩充
    int32_t buffer_size_expand(size_t uDataLen);    // 缓冲区有效数据长度扩充

    void reset();           // 重置当前缓冲区
    
    inline size_t get_totalsize();     // 当前缓冲区总长度
    inline size_t get_buffersize();    // 查看当前缓冲区有效数据长度
    inline unsigned char* get_bufferdata();  // 获取当前缓冲区有效数据起始地址

    inline void set_userdata(void* data);
    inline const void* get_userdata();

private:
    void     *m_userData    = NULL;         // 指向buffer所属的CoConnection

	unsigned char* m_originBuffer = NULL;   // 整个缓冲区起始地址
	size_t m_totalSize      = 0;            // 整个缓存区大小

	unsigned char* m_buffer = NULL;         // 当前有效数据起始地址
	size_t m_bufferSize     = 0;            // 当前有效数据长度
	size_t m_misalignSize   = 0;            // 错位数据长度(可以是已删除数据)  即:m_buffer = m_originBuffer + m_misalignSize
};

size_t CoBuffer::get_totalsize()
{
    return m_totalSize;
}

size_t CoBuffer::get_buffersize()
{
    return m_bufferSize;
}

unsigned char* CoBuffer::get_bufferdata()
{
    return m_buffer;
}

void CoBuffer::set_userdata(void* data)
{
    m_userData = data;
}

const void* CoBuffer::get_userdata()
{
    return m_userData;
}

}

#endif //_CO_BUFFER_H_

