//
// 测试 JsonObj 和 JsonArray 的使用示例
//

#include <iostream>
#include "sonic_helper.h"

void test_basic_usage() {
    std::cout << "=== 测试基本用法 ===" << std::endl;
    JsonObj json {};
    json.AddMember("name", "Alice");
    json.AddMember("age", 25);
    json.AddMember("score", 98.5);

    std::cout << std::string(json) << std::endl;
}

void test_nested_object() {
    std::cout << "\n=== 测试嵌套对象 ===" << std::endl;
    JsonObj json {};
    json.AddMember("name", "Bob");
    json.AddMember("age", 30);

    // 添加嵌套对象
    auto address = json.AddObject("address");
    address.AddMember("city", "Beijing");
    address.AddMember("street", "Zhongguancun");
    address.AddMember("zipcode", 100080);

    std::cout << std::string(json) << std::endl;
}

void test_array() {
    std::cout << "\n=== 测试数组 ===" << std::endl;
    JsonObj json {};
    json.AddMember("name", "Charlie");

    // 添加数组
    auto scores = json.AddArray("scores");
    scores.PushBack(85);
    scores.PushBack(92);
    scores.PushBack(88);
    scores.PushBack(95.5);

    std::cout << std::string(json) << std::endl;
}

void test_array_of_objects() {
    std::cout << "\n=== 测试对象数组 ===" << std::endl;
    JsonObj json {};
    json.AddMember("company", "TechCorp");

    // 添加对象数组
    auto employees = json.AddArray("employees");

    // 第一个员工
    auto emp1 = employees.PushBackObject();
    emp1.AddMember("name", "Alice");
    emp1.AddMember("age", 28);
    emp1.AddMember("position", "Engineer");

    // 第二个员工
    auto emp2 = employees.PushBackObject();
    emp2.AddMember("name", "Bob");
    emp2.AddMember("age", 35);
    emp2.AddMember("position", "Manager");

    std::cout << std::string(json) << std::endl;
}

void test_complex_nested() {
    std::cout << "\n=== 测试复杂嵌套结构 ===" << std::endl;
    JsonObj json {};
    json.AddMember("version", "1.0");
    json.AddMember("timestamp", 1702345678);

    // 添加嵌套对象
    auto data = json.AddObject("data");
    data.AddMember("symbol", "BTC-USDT");
    data.AddMember("price", 45678.90);

    // 在嵌套对象中添加数组
    auto bids = data.AddArray("bids");

    // 在数组中添加对象
    auto bid1 = bids.PushBackObject();
    bid1.AddMember("price", 45650.0);
    bid1.AddMember("volume", 1.5);

    auto bid2 = bids.PushBackObject();
    bid2.AddMember("price", 45640.0);
    bid2.AddMember("volume", 2.3);

    // 添加另一个数组
    auto asks = data.AddArray("asks");
    auto ask1 = asks.PushBackObject();
    ask1.AddMember("price", 45680.0);
    ask1.AddMember("volume", 1.8);

    std::cout << std::string(json) << std::endl;
}

void test_nested_arrays() {
    std::cout << "\n=== 测试嵌套数组 ===" << std::endl;
    JsonObj json {};
    json.AddMember("matrix_name", "test_matrix");

    // 创建二维数组（数组的数组）
    auto matrix = json.AddArray("matrix");

    // 第一行
    auto row1 = matrix.PushBackArray();
    row1.PushBack(1);
    row1.PushBack(2);
    row1.PushBack(3);

    // 第二行
    auto row2 = matrix.PushBackArray();
    row2.PushBack(4);
    row2.PushBack(5);
    row2.PushBack(6);

    std::cout << std::string(json) << std::endl;
}

// ========== JsonHelperV2 测试 (链式访问) ==========

void test_v2_basic_chained() {
    std::cout << "\n=== V2: 基本链式访问 ===" << std::endl;
    JsonHelperV2 json {};
    json["name"] = "Alice";
    json["age"] = 25;
    json["score"] = 98.5;
    std::cout << json.dump() << std::endl;
}

void test_v2_nested() {
    std::cout << "\n=== V2: 嵌套链式访问 ===" << std::endl;
    JsonHelperV2 json {};
    json["user"]["name"] = "Bob";
    json["user"]["age"] = 30;
    json["user"]["address"]["city"] = "Beijing";
    json["user"]["address"]["zipcode"] = 100080;
    std::cout << json.dump() << std::endl;
}

void test_v2_array() {
    std::cout << "\n=== V2: 数组 append ===" << std::endl;
    JsonHelperV2 json {};
    json["name"] = "Charlie";
    json["scores"].append(85);
    json["scores"].append(92);
    json["scores"].append(88);
    std::cout << json.dump() << std::endl;
}

void test_v2_array_of_objects() {
    std::cout << "\n=== V2: 对象数组 ===" << std::endl;
    JsonHelperV2 json {};
    json["company"] = "TechCorp";

    auto emp1 = json["employees"].appendObject();
    emp1["name"] = "Alice";
    emp1["age"] = 28;

    auto emp2 = json["employees"].appendObject();
    emp2["name"] = "Bob";
    emp2["age"] = 35;

    std::cout << json.dump() << std::endl;
}

void test_v2_complex() {
    std::cout << "\n=== V2: 复杂嵌套 ===" << std::endl;
    JsonHelperV2 json {};
    json["version"] = "1.0";
    json["data"]["symbol"] = "BTC-USDT";
    json["data"]["price"] = 45678.90;

    auto bid1 = json["data"]["bids"].appendObject();
    bid1["price"] = 45650.0;
    bid1["volume"] = 1.5;

    std::cout << json.dump() << std::endl;
}

void test_comparison() {
    std::cout << "\n=== 对比 V1 vs V2 ===" << std::endl;

    // V1: 显式调用
    JsonObj v1 {};
    v1.AddMember("name", "Test");
    auto data1 = v1.AddObject("data");
    data1.AddMember("value", 123);
    std::cout << "V1: " << std::string(v1) << std::endl;

    // V2: 链式访问
    JsonHelperV2 v2 {};
    v2["name"] = "Test";
    v2["data"]["value"] = 123;
    std::cout << "V2: " << v2.dump() << std::endl;
}

// ========== 精度控制测试 ==========

void test_precision_basic() {
    std::cout << "\n=== 精度控制: 基本用法 ===" << std::endl;
    const double pi = 3.14159265358979323846;
    const double large = 123456.789012345;

    JsonObj json {};
    json.AddMember("pi_full", pi);                    // 不控制精度
    json.AddMember("pi_2f", pi, ".2f");               // 2位小数
    json.AddMember("pi_4f", pi, ".4f");               // 4位小数
    json.AddMember("pi_6f", pi, ".6f");               // 6位小数
    json.AddMember("large_2f", large, ".2f");         // 大数2位小数
    json.AddMember("large_4g", large, ".4g");         // 4位有效数字
    json.AddMember("large_2e", large, ".2e");         // 科学计数法

    std::cout << std::string(json) << std::endl;
}

void test_precision_array() {
    std::cout << "\n=== 精度控制: 数组 ===" << std::endl;
    JsonObj json {};
    auto prices = json.AddArray("prices");
    prices.PushBack(1.23456789);                      // 不控制精度
    prices.PushBack(1.23456789, ".2f");               // 2位小数
    prices.PushBack(1.23456789, ".4f");               // 4位小数

    std::cout << std::string(json) << std::endl;
}

void test_precision_v2() {
    std::cout << "\n=== 精度控制: V2 链式访问 ===" << std::endl;
    const double value = 9876.54321;

    JsonHelperV2 json {};
    json["full"] = value;                             // 不控制精度
    json["prec_2f"].set(value, ".2f");                // 2位小数
    json["prec_4f"].set(value, ".4f");                // 4位小数

    // 数组中使用精度控制
    json["arr"].append(value);                        // 不控制精度
    json["arr"].append(value, ".2f");                 // 2位小数
    json["arr"].append(value, ".4f");                 // 4位小数

    std::cout << json.dump() << std::endl;
}

int main() {
    try {
        std::cout << "========== JsonObj (V1) 测试 ==========" << std::endl;
        test_basic_usage();
        test_nested_object();
        test_array();
        test_array_of_objects();
        test_complex_nested();
        test_nested_arrays();

        std::cout << "\n========== JsonHelperV2 (链式访问) 测试 ==========" << std::endl;
        test_v2_basic_chained();
        test_v2_nested();
        test_v2_array();
        test_v2_array_of_objects();
        test_v2_complex();
        test_comparison();

        std::cout << "\n========== 精度控制测试 ==========" << std::endl;
        test_precision_basic();
        test_precision_array();
        test_precision_v2();

        std::cout << "\nAll tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
