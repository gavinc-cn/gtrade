//
// 展示原生 sonic_json 接口 vs 当前封装接口的性能对比
//

#include <iostream>
#include <chrono>
#include "sonic_helper.h"

using namespace std::chrono;

// 测试原生 sonic_json 接口
void test_native_api(int iterations) {
    auto start = high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        sonic_json::Document doc {};
        doc.SetObject();
        auto& alloc = doc.GetAllocator();

        // 添加基本字段
        doc.AddMember("name", sonic_json::Node("Alice", alloc), alloc);
        sonic_json::Node age_node {};
        age_node.SetInt64(25);
        doc.AddMember("age", std::move(age_node), alloc);

        // 添加嵌套对象
        sonic_json::Node address {};
        address.SetObject();
        address.AddMember("city", sonic_json::Node("Beijing", alloc), alloc);
        sonic_json::Node zipcode {};
        zipcode.SetInt64(100080);
        address.AddMember("zipcode", std::move(zipcode), alloc);
        doc.AddMember("address", std::move(address), alloc);

        // 添加数组
        sonic_json::Node scores {};
        scores.SetArray();
        sonic_json::Node score1 {};
        score1.SetInt64(85);
        scores.PushBack(std::move(score1), alloc);
        sonic_json::Node score2 {};
        score2.SetInt64(92);
        scores.PushBack(std::move(score2), alloc);
        doc.AddMember("scores", std::move(scores), alloc);

        // 转换为字符串
        std::string result = doc.Dump();
    }

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);

    std::cout << "原生 API: " << iterations << " 次迭代耗时 "
              << duration.count() << " 微秒 (平均 "
              << (double)duration.count() / iterations << " 微秒/次)" << std::endl;
}

// 测试封装后的 JsonObj 接口
void test_helper_api(int iterations) {
    auto start = high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        JsonObj json {};

        // 添加基本字段
        json.AddMember("name", "Alice");
        json.AddMember("age", 25);

        // 添加嵌套对象
        auto address = json.AddObject("address");
        address.AddMember("city", "Beijing");
        address.AddMember("zipcode", 100080);

        // 添加数组
        auto scores = json.AddArray("scores");
        scores.PushBack(85);
        scores.PushBack(92);

        // 转换为字符串
        std::string result = std::string(json);
    }

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);

    std::cout << "封装 API: " << iterations << " 次迭代耗时 "
              << duration.count() << " 微秒 (平均 "
              << (double)duration.count() / iterations << " 微秒/次)" << std::endl;
}

int main() {
    const int ITERATIONS = 10000;

    std::cout << "=== 性能对比测试 ===" << std::endl;
    std::cout << "测试迭代次数: " << ITERATIONS << std::endl << std::endl;

    // 预热
    test_native_api(100);
    test_helper_api(100);

    std::cout << "\n正式测试:" << std::endl;
    test_native_api(ITERATIONS);
    test_helper_api(ITERATIONS);

    return 0;
}
