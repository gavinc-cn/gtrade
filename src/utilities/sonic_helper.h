//
// Created by dell on 2025/3/1.
//

#pragma once

#include "sonic/sonic.h"
#include "str_types.h"
#include <spdlog/fmt/fmt.h>

inline void AddMember(sonic_json::Node& node, sonic_json::Node::AllocatorType& alloc, const std::string& key, const char val) {
    node.AddMember(key, sonic_json::Node(std::string(1, val), alloc), alloc);
}

inline void AddMember(sonic_json::Node& node, sonic_json::Node::AllocatorType& alloc, const std::string& key, const CharCs& val) {
    node.AddMember(key, sonic_json::Node(std::string(val), alloc), alloc);
}

inline void AddMember(sonic_json::Node& node, sonic_json::Node::AllocatorType& alloc, const std::string& key, const double val) {
    sonic_json::Node tmp {};
    tmp.SetDouble(val);
    node.AddMember(key, std::move(tmp), alloc);
}

template <typename T>
std::enable_if_t<zrt::IsIntegralNum_v<T>>
AddMember(sonic_json::Node& node, sonic_json::Node::AllocatorType& alloc, const std::string& key, const T& val) {
    sonic_json::Node tmp {};
    tmp.SetInt64(val);
    node.AddMember(key, std::move(tmp), alloc);
}

template <typename T>
std::enable_if_t<!zrt::IsIntegralNum_v<T> && !zrt::IsFloatingPoint_v<T>>
AddMember(sonic_json::Node& node, sonic_json::Node::AllocatorType& alloc, const std::string& key, const T& val) {
    node.AddMember(key, sonic_json::Node(val, alloc), alloc);
}

// 前向声明
class JsonArray;

// 完善多层json的设计 - 支持嵌套对象和数组
class JsonObj {
public:
    JsonObj() {
        m_doc.SetObject();
        m_node = &m_doc;
    }

    // 用于创建嵌套对象的构造函数
    JsonObj(sonic_json::Node& node, sonic_json::Document::Allocator& alloc):
    m_node(&node), m_alloc(alloc), m_owns_doc(false)
    {
    }

    operator std::string() const {
        if (m_owns_doc) {
            return m_doc.Dump();
        }
        sonic_json::WriteBuffer buffer;
        m_node->Serialize(buffer);
        return buffer.ToString();
    }

    sonic_json::Node& operator[](const std::string& key) const {
        return (*m_node)[key];
    }

    // 基本类型添加方法
    void AddMember(const std::string& key, const char val) const {
        m_node->AddMember(key, sonic_json::Node(std::string(1, val), m_alloc), m_alloc);
    }

    void AddMember(const std::string& key, const CharCs& val) const {
        m_node->AddMember(key, sonic_json::Node(std::string(val), m_alloc), m_alloc);
    }

    void AddMember(const std::string& key, const double val) const {
        sonic_json::Node tmp {};
        tmp.SetDouble(val);
        m_node->AddMember(key, std::move(tmp), m_alloc);
    }

    // 支持 fmt 格式字符串控制精度，如 ".4f", ".6g", ".2e"
    // 格式化后直接存为字符串，避免 double 精度问题
    // 使用方式: json.AddMember("price", 1.23456789, ".4f");  // 输出 "1.2346"
    void AddMember(const std::string& key, const double val, const char* fmt_spec) const {
        const std::string formatted = fmt::format("{:" + std::string(fmt_spec) + "}", val);
        m_node->AddMember(key, sonic_json::Node(formatted, m_alloc), m_alloc);
    }

    void AddMember(const std::string& key, const bool val) const {
        sonic_json::Node tmp {};
        tmp.SetBool(val);
        m_node->AddMember(key, std::move(tmp), m_alloc);
    }

    template <typename T>
    std::enable_if_t<zrt::IsIntNum_v<T>>
    AddMember(const std::string& key, const T& val) const {
        sonic_json::Node tmp {};
        tmp.SetInt64(val);
        m_node->AddMember(key, std::move(tmp), m_alloc);
    }

    template <typename T>
    std::enable_if_t<!zrt::IsIntegralNum_v<T> && !zrt::IsFloatingPoint_v<T>>
    AddMember(const std::string& key, const T& val) const {
        m_node->AddMember(key, sonic_json::Node(val, m_alloc), m_alloc);
    }

    // 添加嵌套对象，返回可以继续操作的 JsonObj
    JsonObj AddObject(const std::string& key) const {
        sonic_json::Node obj {};
        obj.SetObject();
        m_node->AddMember(key, std::move(obj), m_alloc);
        return JsonObj((*m_node)[key], m_alloc);
    }

    // 添加数组，返回可以继续操作的 JsonArray
    JsonArray AddArray(const std::string& key) const;

    // 添加已构建好的对象
    void AddMember(const std::string& key, const JsonObj& helper) const {
        sonic_json::Node copy {};
        copy.CopyFrom(*helper.m_node, m_alloc);
        m_node->AddMember(key, std::move(copy), m_alloc);
    }

    // 获取 allocator（用于高级操作）
    sonic_json::Document::Allocator& GetAllocator() const { return m_alloc; }

    // 获取底层 Node（用于高级操作）
    sonic_json::Node& GetNode() { return *m_node; }
    const sonic_json::Node& GetNode() const { return *m_node; }

private:
    sonic_json::Document m_doc {};
    sonic_json::Node* m_node {nullptr};
    sonic_json::Document::Allocator& m_alloc = m_doc.GetAllocator();
    bool m_owns_doc {true};  // 是否拥有 Document 的所有权
};

// JSON 数组封装类
class JsonArray {
public:
    // 用于创建数组的构造函数
    JsonArray(sonic_json::Node& node, sonic_json::Document::Allocator& alloc):
    m_node(&node), m_alloc(alloc)
    {
    }

    // 添加基本类型元素
    void PushBack(const char val) const {
        m_node->PushBack(sonic_json::Node(std::string(1, val), m_alloc), m_alloc);
    }

    void PushBack(const CharCs& val) const {
        m_node->PushBack(sonic_json::Node(std::string(val), m_alloc), m_alloc);
    }

    void PushBack(const double val) const {
        sonic_json::Node tmp {};
        tmp.SetDouble(val);
        m_node->PushBack(std::move(tmp), m_alloc);
    }

    // 支持 fmt 格式字符串控制精度，如 ".4f", ".6g", ".2e"
    // 格式化后直接存为字符串，避免 double 精度问题
    // 使用方式: arr.PushBack(1.23456789, ".4f");  // 输出 "1.2346"
    void PushBack(const double val, const char* fmt_spec) const {
        const std::string formatted = fmt::format("{:" + std::string(fmt_spec) + "}", val);
        m_node->PushBack(sonic_json::Node(formatted, m_alloc), m_alloc);
    }

    template <typename T>
    std::enable_if_t<zrt::IsIntegralNum_v<T>>
    PushBack(const T& val) {
        sonic_json::Node tmp {};
        tmp.SetInt64(val);
        m_node->PushBack(std::move(tmp), m_alloc);
    }

    template <typename T>
    std::enable_if_t<!zrt::IsIntegralNum_v<T> && !zrt::IsFloatingPoint_v<T>>
    PushBack(const T& val) {
        m_node->PushBack(sonic_json::Node(val, m_alloc), m_alloc);
    }

    // 添加嵌套对象到数组
    JsonObj PushBackObject() const {
        sonic_json::Node obj {};
        obj.SetObject();
        m_node->PushBack(std::move(obj), m_alloc);
        return JsonObj((*m_node)[m_node->Size() - 1], m_alloc);
    }

    // 添加嵌套数组到数组
    JsonArray PushBackArray() const {
        sonic_json::Node arr {};
        arr.SetArray();
        m_node->PushBack(std::move(arr), m_alloc);
        return JsonArray((*m_node)[m_node->Size() - 1], m_alloc);
    }

    // 添加已构建好的对象到数组
    void PushBack(const JsonObj& helper) const {
        sonic_json::Node copy {};
        copy.CopyFrom(helper.GetNode(), m_alloc);
        m_node->PushBack(std::move(copy), m_alloc);
    }

    // 获取数组大小
    size_t Size() const { return m_node->Size(); }

    // 获取底层 Node（用于高级操作）
    sonic_json::Node& GetNode() { return *m_node; }
    const sonic_json::Node& GetNode() const { return *m_node; }

private:
    sonic_json::Node* m_node {nullptr};
    sonic_json::Document::Allocator& m_alloc;
};

// 实现 JsonObj::AddArray
inline JsonArray JsonObj::AddArray(const std::string& key) const {
    sonic_json::Node arr {};
    arr.SetArray();
    m_node->AddMember(key, std::move(arr), m_alloc);
    return JsonArray((*m_node)[key], m_alloc);
}

// ============================================================================
// JsonHelperV2 - 支持链式访问的版本 (例如: json["key1"]["key2"] = value)
// 注意: 这个版本更易用，但性能略低于 JsonObj（约 15-30% 额外开销）
// 推荐用于配置文件、日志等非性能关键路径
// ============================================================================

// 前向声明
class JsonHelperV2;

// JSON 代理对象 - 支持链式访问和赋值
class JsonProxy {
public:
    JsonProxy(sonic_json::Node* node, sonic_json::Document::Allocator& alloc)
        : m_node(node), m_alloc(alloc) {}

    // 支持链式访问: json["key1"]["key2"]
    JsonProxy operator[](const std::string& key) const {
        // 如果当前节点不是对象，转换为对象
        if (!m_node->IsObject()) {
            sonic_json::Node obj {};
            obj.SetObject();
            *m_node = std::move(obj);
        }

        // 如果 key 不存在，自动创建
        if (!m_node->HasMember(key)) {
            sonic_json::Node obj {};
            obj.SetObject();
            m_node->AddMember(key, std::move(obj), m_alloc);
        }

        return JsonProxy(&(*m_node)[key], m_alloc);
    }

    // 支持数组下标访问: json["array"][0]
    JsonProxy operator[](const int index) const {
        // 如果当前节点不是数组，转换为数组
        if (!m_node->IsArray()) {
            sonic_json::Node arr {};
            arr.SetArray();
            *m_node = std::move(arr);
        }

        // 确保数组有足够的元素
        while (static_cast<int>(m_node->Size()) <= index) {
            sonic_json::Node obj {};
            obj.SetObject();
            m_node->PushBack(std::move(obj), m_alloc);
        }

        return JsonProxy(&(*m_node)[index], m_alloc);
    }

    // 支持赋值: json["key"] = value
    JsonProxy& operator=(const char val) {
        *m_node = sonic_json::Node(std::string(1, val), m_alloc);
        return *this;
    }

    JsonProxy& operator=(const CharCs& val) {
        *m_node = sonic_json::Node(std::string(val), m_alloc);
        return *this;
    }

    JsonProxy& operator=(const std::string& val) {
        *m_node = sonic_json::Node(val, m_alloc);
        return *this;
    }

    JsonProxy& operator=(const char* val) {
        *m_node = sonic_json::Node(std::string(val), m_alloc);
        return *this;
    }

    JsonProxy& operator=(double val) {
        m_node->SetDouble(val);
        return *this;
    }

    // 支持 fmt 格式字符串控制精度，如 ".4f", ".6g", ".2e"
    // 格式化后直接存为字符串，避免 double 精度问题
    // 使用方式: json["price"].set(1.23456789, ".4f");  // 输出 "1.2346"
    JsonProxy& set(const double val, const char* fmt_spec) {
        const std::string formatted = fmt::format("{:" + std::string(fmt_spec) + "}", val);
        *m_node = sonic_json::Node(formatted, m_alloc);
        return *this;
    }

    template <typename T>
    std::enable_if_t<zrt::IsIntegralNum_v<T>, JsonProxy&>
    operator=(const T& val) {
        m_node->SetInt64(val);
        return *this;
    }

    // 支持 append() 操作: json["array"].append(value)
    void append(const char val) const {
        ensureArray();
        m_node->PushBack(sonic_json::Node(std::string(1, val), m_alloc), m_alloc);
    }

    void append(const CharCs& val) const {
        ensureArray();
        m_node->PushBack(sonic_json::Node(std::string(val), m_alloc), m_alloc);
    }

    void append(const std::string& val) const {
        ensureArray();
        m_node->PushBack(sonic_json::Node(val, m_alloc), m_alloc);
    }

    void append(const char* val) const {
        ensureArray();
        m_node->PushBack(sonic_json::Node(std::string(val), m_alloc), m_alloc);
    }

    void append(const double val) const {
        ensureArray();
        sonic_json::Node tmp {};
        tmp.SetDouble(val);
        m_node->PushBack(std::move(tmp), m_alloc);
    }

    // 支持 fmt 格式字符串控制精度，如 ".4f", ".6g", ".2e"
    // 格式化后直接存为字符串，避免 double 精度问题
    // 使用方式: json["arr"].append(1.23456789, ".4f");  // 输出 "1.2346"
    void append(const double val, const char* fmt_spec) const {
        ensureArray();
        const std::string formatted = fmt::format("{:" + std::string(fmt_spec) + "}", val);
        m_node->PushBack(sonic_json::Node(formatted, m_alloc), m_alloc);
    }

    template <typename T>
    std::enable_if_t<zrt::IsIntegralNum_v<T>>
    append(const T& val) const {
        ensureArray();
        sonic_json::Node tmp {};
        tmp.SetInt64(val);
        m_node->PushBack(std::move(tmp), m_alloc);
    }

    // append 对象
    JsonProxy appendObject() {
        ensureArray();
        sonic_json::Node obj {};
        obj.SetObject();
        m_node->PushBack(std::move(obj), m_alloc);
        return JsonProxy(&(*m_node)[m_node->Size() - 1], m_alloc);
    }

    // append 数组
    JsonProxy appendArray() {
        ensureArray();
        sonic_json::Node arr {};
        arr.SetArray();
        m_node->PushBack(std::move(arr), m_alloc);
        return JsonProxy(&(*m_node)[m_node->Size() - 1], m_alloc);
    }

    // 创建嵌套对象
    JsonProxy addObject(const std::string& key) const {
        if (!m_node->IsObject()) {
            sonic_json::Node obj {};
            obj.SetObject();
            *m_node = std::move(obj);
        }

        sonic_json::Node obj {};
        obj.SetObject();
        m_node->AddMember(key, std::move(obj), m_alloc);
        return JsonProxy(&(*m_node)[key], m_alloc);
    }

    // 创建数组
    JsonProxy addArray(const std::string& key) const {
        if (!m_node->IsObject()) {
            sonic_json::Node obj {};
            obj.SetObject();
            *m_node = std::move(obj);
        }

        sonic_json::Node arr {};
        arr.SetArray();
        m_node->AddMember(key, std::move(arr), m_alloc);
        return JsonProxy(&(*m_node)[key], m_alloc);
    }

    // 获取数组大小
    size_t size() const {
        if (m_node->IsArray()) {
            return m_node->Size();
        }
        return 0;
    }

    // 检查是否为空
    bool empty() const {
        if (m_node->IsArray() || m_node->IsObject()) {
            return m_node->Size() == 0;
        }
        return true;
    }

    // 获取底层 Node
    sonic_json::Node& getNode() { return *m_node; }
    const sonic_json::Node& getNode() const { return *m_node; }

private:
    void ensureArray() const {
        if (!m_node->IsArray()) {
            sonic_json::Node arr {};
            arr.SetArray();
            *m_node = std::move(arr);
        }
    }

    sonic_json::Node* m_node {nullptr};
    sonic_json::Document::Allocator& m_alloc;
};

// JsonHelperV2 - 支持链式访问的 JSON 构建器
class JsonHelperV2 {
public:
    JsonHelperV2() {
        m_doc.SetObject();
    }

    // 支持链式访问: json["key"]
    JsonProxy operator[](const std::string& key) {
        // 如果 key 不存在，自动创建对象节点
        if (!m_doc.HasMember(key)) {
            sonic_json::Node obj {};
            obj.SetObject();
            m_doc.AddMember(key, std::move(obj), m_doc.GetAllocator());
        }
        return JsonProxy(&m_doc[key], m_doc.GetAllocator());
    }

    // 支持数组下标访问
    JsonProxy operator[](int index) {
        // 根节点转换为数组
        if (!m_doc.IsArray()) {
            m_doc.SetArray();
        }

        // 确保数组有足够的元素
        while (static_cast<int>(m_doc.Size()) <= index) {
            sonic_json::Node obj {};
            obj.SetObject();
            m_doc.PushBack(std::move(obj), m_doc.GetAllocator());
        }

        return JsonProxy(&m_doc[index], m_doc.GetAllocator());
    }

    // 转换为字符串
    operator std::string() const {
        return m_doc.Dump();
    }

    std::string dump() const {
        return m_doc.Dump();
    }

    // 创建嵌套对象
    JsonProxy addObject(const std::string& key) {
        sonic_json::Node obj {};
        obj.SetObject();
        m_doc.AddMember(key, std::move(obj), m_doc.GetAllocator());
        return JsonProxy(&m_doc[key], m_doc.GetAllocator());
    }

    // 创建数组
    JsonProxy addArray(const std::string& key) {
        sonic_json::Node arr {};
        arr.SetArray();
        m_doc.AddMember(key, std::move(arr), m_doc.GetAllocator());
        return JsonProxy(&m_doc[key], m_doc.GetAllocator());
    }

    // 获取底层 Document
    sonic_json::Document& getDocument() { return m_doc; }
    const sonic_json::Document& getDocument() const { return m_doc; }

private:
    sonic_json::Document m_doc {};
};
