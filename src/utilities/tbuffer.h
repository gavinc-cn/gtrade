#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <type_traits>
#include <boost/interprocess/managed_shared_memory.hpp>
//#include <boost/interprocess/allocators/allocator.hpp>
//#include <boost/unordered_map.hpp>
//#include <boost/functional/hash.hpp>

// 编译期类型检查：TBuffer 只支持 trivially copyable 类型（POD 类型）
//
// 原因：TBuffer 通过值拷贝将数据序列化到连续内存中，用于跨线程传递。
// 这要求：
// 1. 对象可以通过简单的内存拷贝来复制（memcpy 安全）
// 2. 不需要调用构造函数/析构函数来管理资源
// 3. 没有动态分配的成员（如 std::string, std::vector）
//
// 如果需要传递包含动态内存的数据，请使用固定长度的 char 数组替代：
//   ❌ 错误: struct { std::string name; };
//   ✅ 正确: struct { char name[64]; };
#define TBUFFER_REQUIRE_TRIVIALLY_COPYABLE(T) \
    static_assert(std::is_trivially_copyable<T>::value, \
        "TBuffer only supports trivially copyable types (POD types). " \
        "The type '" #T "' contains non-POD members (like std::string, std::vector). " \
        "Please use fixed-size char arrays instead.");

//////////////////////////////////////////////////////////////////////////
// 存储发送数据的缓存
class TBuffer
{
public:
	TBuffer();
	TBuffer(unsigned int nSize);
    TBuffer(const char* pBuffer, unsigned int nLength);

    template<typename T, typename = std::enable_if_t<!std::is_integral<T>::value>>
    TBuffer(const T& data): TBuffer(sizeof(data)) {
        Append(data);
    }

	~TBuffer();

	void Reset();							// 回收空间，清空数据
	void Reverse(unsigned int nSize);		// 保留空间
    void ReverseMore(unsigned int nSize);   // 保留更多空间
	void CopyBuffer(const char* pBuffer, unsigned int nLength);
	const char* Data() const;				// 返回缓存指针
	template<typename T>
	const T& Data() const {return *reinterpret_cast<const T*>(Data());}
    char* MutableData() const;    				// 返回缓存指针
	unsigned int GetSize() const;			// 返回数据大小
	unsigned int GetCapacity() const;		// 返回容量大小

	// 为了性能考虑，解决多次拷贝的问题，提供直接访问数据缓存的接口
	// 该接口返回可用的缓存区，并直接增加nLength作为有效数据长度，因此下一次修改调用之前需要确保数据已经按照规定长度复制完毕
	char* Capacity(unsigned int nLength);

    template<typename T, typename... Args>
    TBuffer& Append(const T& data, const Args&... rest) {
        CopyBuffer(data);
        return Append(rest...);
    }

    TBuffer& Append() {
        return *this;
    }

    // unsigned int GetCount() const {
        // return m_data_index_vec.size();
    // }

    template<typename T>
    const T& RefData() const {
        return *reinterpret_cast<const T*>(Data());
    }

    // template<typename T>
    // const T& GetData(unsigned int count) const {
    //     return *reinterpret_cast<T*>(&m_pBuffer[m_data_index_vec[count]]);
    // }

    // std::string GetAttr(const std::string& key) {
    //     auto iter = m_attribute.find(key);
    //     if (iter != m_attribute.end()) {
    //         return iter->second;
    //     }
    //     return {};
    // }

    // void SetAttr(const std::string& key, const std::string& val) {
    //     m_attribute[key] = val;
    // }
    //
    // void SetAttr(const TBuffer& other) {
    //     m_attribute = other.m_attribute;
    // }

    bool SetShm(const std::string& shm_name);
    static bool HasShm(const std::string& shm_name);

    template<typename T>
    int ForEach(const std::function<void(const T&)>& func);

private:
    template<typename T>
    void CopyBuffer(const T& data)
    {
        // 编译期检查：只允许 trivially copyable 类型（C++14 兼容）
        TBUFFER_REQUIRE_TRIVIALLY_COPYABLE(T);

        // 确保有足够的空间
        ReverseMore(m_nLength + sizeof(T));
        *reinterpret_cast<T*>(&m_pBuffer[m_nLength]) = data;
        // m_data_index_vec.emplace_back(m_nLength);
        m_nLength += sizeof(T);
    }

    // 字节对齐的大小
	static unsigned int AllignedSize(unsigned int nSize);
    // 确保内存能够容纳nSize数据，如果没有空间则开辟空间，如果原来已经存在数据，则复制数据到新的空间
	void Allocate(unsigned int nSize);

	char* m_pBuffer {};
	unsigned int m_nLength {};
	unsigned int m_nCapacity {};		// 缓存空间容量
	// std::vector<unsigned int> m_data_index_vec {};
	// std::unordered_map<std::string,std::string> m_attribute {};
	// std::string m_shm_name {};
    // std::shared_ptr<boost::interprocess::managed_shared_memory> m_shm_segment {};
};

template<typename T>
int TBuffer::ForEach(const std::function<void(const T&)>& func) {
    const T* data_ptr = reinterpret_cast<const T*>(Data());
    int count = 0;
    for (uint i = 0; i < GetSize()/sizeof(T); ++i) {
        func(data_ptr[i]);
        ++count;
    }
    return count;
}

typedef std::shared_ptr<TBuffer> TBufferPtr;
