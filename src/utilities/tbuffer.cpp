#include "tbuffer.h"
#ifdef __linux__
#include <cstring>
#else
#include <cstringt.h>
#endif
#include <boost/assert.hpp>
#include "spdlog/spdlog.h"

//////////////////////////////////////////////////////////////////////////
// class ToSendData
#define MIN_BLOCK_SIZE 1024							// 保留数据大小
#define MALLOC_ALIGN_MASK (sizeof(void*) - 1)		// 字节对齐掩码
TBuffer::TBuffer()
{
	m_pBuffer = nullptr;
	m_nCapacity = 0;
	m_nLength = 0;
	Allocate(MIN_BLOCK_SIZE);
}

TBuffer::TBuffer(const unsigned int nSize)
{
	m_pBuffer = nullptr;
	m_nCapacity = 0;
	m_nLength = 0;
	Allocate(nSize);
}

TBuffer::TBuffer(const char* pBuffer, unsigned int nLength)
{
	m_pBuffer = nullptr;
	m_nCapacity = 0;
	m_nLength = 0;
	Allocate(nLength);
	memcpy(m_pBuffer, pBuffer, nLength);
	// m_data_index_vec.emplace_back(m_nLength);
	m_nLength = nLength;
}

TBuffer::~TBuffer()
{
	if (m_pBuffer != nullptr)
	{
		delete[] m_pBuffer;
		m_pBuffer = nullptr;
	}
	m_nLength = 0;
	m_nCapacity = 0;
	// if (m_shm_segment && !m_shm_name.empty()) {
        // boost::interprocess::shared_memory_object::remove(m_shm_name.c_str());
	// }
}

void TBuffer::Reset()
{
	m_nLength = 0;
}

void TBuffer::Reverse(const unsigned int nSize)
{
	if (nSize <= m_nCapacity)
		return;
	Allocate(nSize);
}

void TBuffer::ReverseMore(const unsigned int nSize)
{
    if (nSize <= m_nCapacity)
        return;
    Allocate(nSize + m_nCapacity);
}

void TBuffer::CopyBuffer(const char* pBuffer, unsigned int nLength)
{
	// 确保有足够的空间
	Reverse(m_nLength + nLength);
	memcpy(&m_pBuffer[m_nLength], pBuffer, nLength);
	// m_data_index_vec.emplace_back(m_nLength);
	m_nLength += nLength;
}

const char* TBuffer::Data() const
{
	BOOST_ASSERT(m_pBuffer != NULL);
	return m_pBuffer;
}

char* TBuffer::MutableData() const {
    BOOST_ASSERT(m_pBuffer != NULL);
    return m_pBuffer;
}


unsigned int TBuffer::GetSize() const
{
	return m_nLength;
}

unsigned int TBuffer::GetCapacity() const
{
	return m_nCapacity;
}

// 为了性能考虑，解决多次拷贝的问题，提供直接访问数据缓存的接口
// 该接口返回可用的缓存区，并直接增加nLength作为有效数据长度，因此下一次修改调用之前需要确保数据已经按照规定长度复制完毕
char* TBuffer::Capacity(unsigned int nLength)
{
	Reverse(m_nLength + nLength);
	char *pData = &m_pBuffer[m_nLength];
	m_nLength += nLength;
	return pData;
}

unsigned int TBuffer::AllignedSize(const unsigned int nSize)
{
	return (nSize + MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK;
}

void TBuffer::Allocate(unsigned int nSize)
{
	if (nSize <= MIN_BLOCK_SIZE)
	{
		// 最小保留BLOCK_SIZE
		nSize = MIN_BLOCK_SIZE;
	}
	else
	{
		// 字节对齐后申请大小
		nSize = AllignedSize(nSize);
	}
	if (m_pBuffer != NULL)
	{
		char* pTemp = new char[nSize] {};
		memcpy(pTemp, m_pBuffer, m_nLength);
		delete[] m_pBuffer;
		m_pBuffer = pTemp;
	}
	else
	{
		m_pBuffer = new char[nSize] {};
	}
	m_nCapacity = nSize;
}

// bool TBuffer::SetShm(const std::string& shm_name) {
//     m_shm_name = shm_name;
//     try {
//         m_shm_segment = std::make_unique<boost::interprocess::managed_shared_memory>(boost::interprocess::open_or_create, shm_name.c_str(), 512);
//         return true;
//     } catch (const std::exception& e) {
//         SPDLOG_ERROR("allocate shm({}) failed: {}", shm_name, e.what());
//         return false;
//     }
// }
//
// bool TBuffer::HasShm(const std::string& shm_name) {
// #ifdef __linux__
//     int fd = shm_open(shm_name.c_str(), O_RDONLY, 0);
//     if (fd == -1) {
//         return false;
//     } else {
//         close(fd);
//         return true;
//     }
// #else
//     return false;
// #endif // __linux__
// }
