// 打桩：unit_test 不链接 message_server，OrderManager 的 Save*2Shm 失败路径里的
// 消息通知替换为空操作。签名与 src/message_server/notify_msg_helper.h 保持一致
// （全局命名空间，3 个 std::string 参数；直接包含该头文件以保证签名一致）。
#include "notify_msg_helper.h"

void SendNotifyMsg(const std::string& channel, const std::string& subject, const std::string& message) {
    (void)channel;
    (void)subject;
    (void)message;
}
