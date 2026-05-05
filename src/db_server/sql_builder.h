//
// SQL Builder Template - 基于模板的SQL构建器
// 自动生成SQL语句，字段与XML定义同步
//

#pragma once

#include <string>
#include <vector>
#include <array>
#include <string_view>
#include <algorithm>
#include <spdlog/fmt/fmt.h>

namespace gtrade {

// 字段类型枚举
enum class SqlFieldType {
    kInt,
    kInt64,
    kUInt16,
    kUInt32,
    kUInt64,
    kDouble,
    kFloat,
    kChar,
    kString,
    kCharArray
};

// 字段元数据结构
struct FieldMetadata {
    std::string_view name;        // 字段名
    SqlFieldType type;            // 字段类型
    bool needs_escape{false};     // 是否需要转义（字符串类型）
};

// FieldTraits 基础模板（需要为每个结构体特化）
template<typename T>
struct FieldTraits {
    // 必须在特化中定义：
    // static constexpr std::string_view kTableName;
    // static constexpr std::array<FieldMetadata, N> kFields;
    // static constexpr std::array<std::string_view, M> kPrimaryKeys;
    // static std::string GetFieldValue(const T& data, size_t field_index);
};

// SQL Builder 模板类
template<typename T>
class SqlBuilder {
public:
    using Traits = FieldTraits<T>;

    // ============ 基础构建函数 ============

    // 构建字段列表: `field1`, `field2`, ...
    static std::string BuildFieldList() {
        std::vector<std::string> fields;
        fields.reserve(Traits::kFields.size());

        for (const auto& field : Traits::kFields) {
            fields.push_back(fmt::format("`{}`", field.name));
        }

        return fmt::format("{}", fmt::join(fields, ", "));
    }

    // 构建值列表: 123, 'abc', 45.6, ...
    static std::string BuildValueList(const T& data) {
        std::vector<std::string> values;
        values.reserve(Traits::kFields.size());

        for (size_t i = 0; i < Traits::kFields.size(); ++i) {
            values.push_back(Traits::GetFieldValue(data, i));
        }

        return fmt::format("{}", fmt::join(values, ", "));
    }

    // 构建 UPDATE 子句: field1=value1, field2=value2, ...
    // exclude_fields: 排除的字段名（通常是主键）
    static std::string BuildUpdateClause(const T& data, const std::vector<std::string_view>& exclude_fields = {}) {
        std::vector<std::string> clauses;

        for (size_t i = 0; i < Traits::kFields.size(); ++i) {
            const auto& field = Traits::kFields[i];

            // 检查是否在排除列表中
            bool is_excluded = std::find(exclude_fields.begin(), exclude_fields.end(), field.name)
                             != exclude_fields.end();
            if (is_excluded) {
                continue;
            }

            std::string value = Traits::GetFieldValue(data, i);
            clauses.push_back(fmt::format("{}={}", field.name, value));
        }

        return fmt::format("{}", fmt::join(clauses, ", "));
    }

    // ============ SQL 语句构建 ============

    // INSERT INTO table (fields) VALUES (values)
    static std::string BuildInsertSql(const T& data, std::string_view table_name = Traits::kTableName) {
        return fmt::format(
            "INSERT INTO `{}` ({}) VALUES ({})",
            table_name,
            BuildFieldList(),
            BuildValueList(data)
        );
    }

    // INSERT INTO table (fields) VALUES (values) ON DUPLICATE KEY UPDATE ...
    static std::string BuildInsertOrUpdateSql(const T& data, std::string_view table_name = Traits::kTableName) {
        // 排除主键字段
        std::vector<std::string_view> exclude_fields(Traits::kPrimaryKeys.begin(), Traits::kPrimaryKeys.end());

        return fmt::format(
            "INSERT INTO `{}` ({}) VALUES ({}) "
            "ON DUPLICATE KEY UPDATE {}, update_time=CURRENT_TIMESTAMP",
            table_name,
            BuildFieldList(),
            BuildValueList(data),
            BuildUpdateClause(data, exclude_fields)
        );
    }

    // REPLACE INTO table (fields) VALUES (values)
    static std::string BuildReplaceSql(const T& data, std::string_view table_name = Traits::kTableName) {
        return fmt::format(
            "REPLACE INTO `{}` ({}) VALUES ({})",
            table_name,
            BuildFieldList(),
            BuildValueList(data)
        );
    }

    // DELETE FROM table WHERE primary_key=value
    static std::string BuildDeleteSql(const T& data, std::string_view table_name = Traits::kTableName) {
        std::vector<std::string> where_clauses;

        for (const auto& pk : Traits::kPrimaryKeys) {
            // 找到主键字段的索引
            for (size_t i = 0; i < Traits::kFields.size(); ++i) {
                if (Traits::kFields[i].name == pk) {
                    std::string value = Traits::GetFieldValue(data, i);
                    where_clauses.push_back(fmt::format("{}={}", pk, value));
                    break;
                }
            }
        }

        return fmt::format(
            "DELETE FROM `{}` WHERE {}",
            table_name,
            fmt::join(where_clauses, " AND ")
        );
    }

    // ============ 部分字段更新 ============

    // 只更新指定的字段
    static std::string BuildPartialUpdateSql(
        const T& data,
        const std::vector<std::string_view>& update_fields,
        std::string_view table_name = Traits::kTableName) {

        std::vector<std::string> set_clauses;

        for (const auto& field_name : update_fields) {
            // 找到字段索引
            for (size_t i = 0; i < Traits::kFields.size(); ++i) {
                if (Traits::kFields[i].name == field_name) {
                    std::string value = Traits::GetFieldValue(data, i);
                    set_clauses.push_back(fmt::format("{}={}", field_name, value));
                    break;
                }
            }
        }

        set_clauses.push_back("update_time=CURRENT_TIMESTAMP");

        return fmt::format(
            "INSERT INTO `{}` ({}) VALUES ({}) "
            "ON DUPLICATE KEY UPDATE {}",
            table_name,
            BuildFieldList(),
            BuildValueList(data),
            fmt::join(set_clauses, ", ")
        );
    }

    // ============ 辅助函数 ============

    // 获取字段数量
    static constexpr size_t GetFieldCount() {
        return Traits::kFields.size();
    }

    // 获取表名
    static constexpr std::string_view GetTableName() {
        return Traits::kTableName;
    }
};

} // namespace gtrade
