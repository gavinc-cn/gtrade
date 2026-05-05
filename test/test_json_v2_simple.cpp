//
// 简单测试 JsonHelperV2
//

#include <iostream>
#include "sonic_helper.h"

int main() {
    // 测试 1: 基本链式访问
    JsonHelperV2 json1 {};
    json1["name"] = "Alice";
    json1["age"] = 25;
    std::cout << "Test 1: " << json1.dump() << std::endl;

    // 测试 2: 嵌套访问
    JsonHelperV2 json2 {};
    json2["user"]["name"] = "Bob";
    json2["user"]["age"] = 30;
    std::cout << "Test 2: " << json2.dump() << std::endl;

    // 测试 3: 数组
    JsonHelperV2 json3 {};
    json3["scores"].append(85);
    json3["scores"].append(92);
    std::cout << "Test 3: " << json3.dump() << std::endl;

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
