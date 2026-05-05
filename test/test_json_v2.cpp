//
// 测试 JsonHelperV2 链式访问接口
//

#include <iostream>
#include "sonic_helper.h"

void test_basic_chained_access() {
    std::cout << "=== 测试基本链式访问 ===" << std::endl;

    JsonHelperV2 json {};
    json["name"] = "Alice";
    json["age"] = 25;
    json["score"] = 98.5;

    std::cout << std::string(json) << std::endl;
}

void test_nested_chained_access() {
    std::cout << "\n=== 测试嵌套链式访问 ===" << std::endl;

    JsonHelperV2 json {};
    json["user"]["name"] = "Bob";
    json["user"]["age"] = 30;
    json["user"]["address"]["city"] = "Beijing";
    json["user"]["address"]["street"] = "Zhongguancun";
    json["user"]["address"]["zipcode"] = 100080;

    std::cout << std::string(json) << std::endl;
}

void test_array_append() {
    std::cout << "\n=== 测试数组 append() ===" << std::endl;

    JsonHelperV2 json {};
    json["name"] = "Charlie";
    json["scores"].append(85);
    json["scores"].append(92);
    json["scores"].append(88);
    json["scores"].append(95.5);

    std::cout << std::string(json) << std::endl;
}

void test_array_of_objects() {
    std::cout << "\n=== 测试对象数组 ===" << std::endl;

    JsonHelperV2 json {};
    json["company"] = "TechCorp";

    // 方式 1: 使用 appendObject()
    auto emp1 = json["employees"].appendObject();
    emp1["name"] = "Alice";
    emp1["age"] = 28;
    emp1["position"] = "Engineer";

    auto emp2 = json["employees"].appendObject();
    emp2["name"] = "Bob";
    emp2["age"] = 35;
    emp2["position"] = "Manager";

    std::cout << std::string(json) << std::endl;
}

void test_complex_nested() {
    std::cout << "\n=== 测试复杂嵌套结构 ===" << std::endl;

    JsonHelperV2 json {};
    json["version"] = "1.0";
    json["timestamp"] = 1702345678;

    // 嵌套对象
    json["data"]["symbol"] = "BTC-USDT";
    json["data"]["price"] = 45678.90;

    // 嵌套对象中的数组
    auto bid1 = json["data"]["bids"].appendObject();
    bid1["price"] = 45650.0;
    bid1["volume"] = 1.5;

    auto bid2 = json["data"]["bids"].appendObject();
    bid2["price"] = 45640.0;
    bid2["volume"] = 2.3;

    auto ask1 = json["data"]["asks"].appendObject();
    ask1["price"] = 45680.0;
    ask1["volume"] = 1.8;

    std::cout << std::string(json) << std::endl;
}

void test_array_index_access() {
    std::cout << "\n=== 测试数组下标访问 ===" << std::endl;

    JsonHelperV2 json {};
    json["matrix"][0].append(1);
    json["matrix"][0].append(2);
    json["matrix"][0].append(3);

    json["matrix"][1].append(4);
    json["matrix"][1].append(5);
    json["matrix"][1].append(6);

    std::cout << std::string(json) << std::endl;
}

void test_mixed_usage() {
    std::cout << "\n=== 测试混合使用方式 ===" << std::endl;

    JsonHelperV2 json {};

    // 链式访问
    json["order"]["symbol"] = "BTC-USDT";
    json["order"]["side"] = "buy";
    json["order"]["price"] = 45000.0;

    // 数组 append
    json["order"]["tags"].append("high-priority");
    json["order"]["tags"].append("limit-order");

    // 嵌套对象
    json["order"]["metadata"]["trader_id"] = 12345;
    json["order"]["metadata"]["timestamp"] = 1702345678;

    std::cout << std::string(json) << std::endl;
}

void test_comparison_with_v1() {
    std::cout << "\n=== 对比 JsonObj vs JsonHelperV2 ===" << std::endl;

    // JsonObj (v1) - 显式方法调用
    std::cout << "JsonObj (v1):" << std::endl;
    JsonObj json1 {};
    json1.AddMember("name", "Test");
    auto data1 = json1.AddObject("data");
    data1.AddMember("value", 123);
    auto arr1 = data1.AddArray("items");
    arr1.PushBack(1);
    arr1.PushBack(2);
    std::cout << std::string(json1) << std::endl;

    // JsonHelperV2 - 链式访问
    std::cout << "\nJsonHelperV2 (v2):" << std::endl;
    JsonHelperV2 json2 {};
    json2["name"] = "Test";
    json2["data"]["value"] = 123;
    json2["data"]["items"].append(1);
    json2["data"]["items"].append(2);
    std::cout << std::string(json2) << std::endl;

    std::cout << "\n两种方式生成的 JSON 内容相同，但 v2 语法更简洁" << std::endl;
}

int main() {
    try {
        test_basic_chained_access();
        test_nested_chained_access();
        test_array_append();
        test_array_of_objects();
        test_complex_nested();
        test_array_index_access();
        test_mixed_usage();
        test_comparison_with_v1();

        std::cout << "\n✅ 所有测试完成！" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ 错误: " << e.what() << std::endl;
        return 1;
    }
}
