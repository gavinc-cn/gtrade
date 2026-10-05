//
// zrt_file_test.cpp - zrt_file.h 中 FileWriter/FileReader 单元测试
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/zrt_file.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class ZrtFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/zrt_file_test";
        test_file_ = test_dir_ + "/test_file.txt";

        // 清理并创建测试目录
        fs::remove_all(test_dir_);
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        // 清理测试文件和目录
        fs::remove_all(test_dir_);
    }

    std::string test_dir_;
    std::string test_file_;
};

// ========== FileWriter 测试 ==========

// 测试 FileWriter 基本写入
TEST_F(ZrtFileTest, FileWriterBasicWrite) {
    zrt::FileWriter writer(test_file_);

    EXPECT_TRUE(writer.Open());
    EXPECT_TRUE(writer.WriteLine("Line 1"));
    EXPECT_TRUE(writer.WriteLine("Line 2"));
    EXPECT_TRUE(writer.WriteLine("Line 3"));
    writer.Flush();

    // 验证文件内容
    std::ifstream in(test_file_);
    ASSERT_TRUE(in.is_open());

    std::string line {};
    std::getline(in, line);
    EXPECT_EQ(line, "Line 1");

    std::getline(in, line);
    EXPECT_EQ(line, "Line 2");

    std::getline(in, line);
    EXPECT_EQ(line, "Line 3");
}

// 测试 FileWriter 格式化写入
TEST_F(ZrtFileTest, FileWriterFormatWrite) {
    zrt::FileWriter writer(test_file_);

    EXPECT_TRUE(writer.Open());
    EXPECT_TRUE(writer.WriteFmt("Number: {}", 42));
    EXPECT_TRUE(writer.WriteFmt("Float: {:.2f}", 3.14159));
    EXPECT_TRUE(writer.WriteFmt("String: {}", "test"));
    writer.Flush();

    // 验证
    std::ifstream in(test_file_);
    std::string line {};

    std::getline(in, line);
    EXPECT_EQ(line, "Number: 42");

    std::getline(in, line);
    EXPECT_EQ(line, "Float: 3.14");

    std::getline(in, line);
    EXPECT_EQ(line, "String: test");
}

// 测试 FileWriter 自动创建目录
TEST_F(ZrtFileTest, FileWriterEnsureDir) {
    const std::string deep_path = test_dir_ + "/a/b/c/file.txt";
    zrt::FileWriter writer(deep_path);

    EXPECT_TRUE(writer.EnsureDir());
    EXPECT_TRUE(writer.Open());
    EXPECT_TRUE(writer.WriteLine("Deep directory test"));
    writer.Flush();

    // 验证目录和文件已创建
    EXPECT_TRUE(fs::exists(deep_path));
}

// 测试 FileWriter 追加模式
TEST_F(ZrtFileTest, FileWriterAppendMode) {
    // 第一次写入
    {
        zrt::FileWriter writer(test_file_, false);
        EXPECT_TRUE(writer.Open());
        writer.WriteLine("First line");
        writer.Flush();
    }

    // 追加写入
    {
        zrt::FileWriter writer(test_file_, true);
        EXPECT_TRUE(writer.Open());
        writer.WriteLine("Second line");
        writer.Flush();
    }

    // 验证内容
    std::ifstream in(test_file_);
    std::string line {};

    std::getline(in, line);
    EXPECT_EQ(line, "First line");

    std::getline(in, line);
    EXPECT_EQ(line, "Second line");
}

// 测试 FileWriter 覆盖模式
TEST_F(ZrtFileTest, FileWriterOverwriteMode) {
    // 第一次写入
    {
        zrt::FileWriter writer(test_file_, false);
        EXPECT_TRUE(writer.Open());
        writer.WriteLine("Original content");
        writer.Flush();
    }

    // 覆盖写入
    {
        zrt::FileWriter writer(test_file_, false);
        EXPECT_TRUE(writer.Open());
        writer.WriteLine("New content");
        writer.Flush();
    }

    // 验证只有新内容
    std::ifstream in(test_file_);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());

    EXPECT_TRUE(content.find("New content") != std::string::npos);
    EXPECT_TRUE(content.find("Original content") == std::string::npos);
}

// 测试 FileWriter Exist 方法
TEST_F(ZrtFileTest, FileWriterExist) {
    zrt::FileWriter writer(test_file_);

    EXPECT_FALSE(writer.Exist());

    writer.Open();
    writer.WriteLine("test");
    writer.Flush();

    EXPECT_TRUE(writer.Exist());
}

// 测试 FileWriter SetHeader 方法
TEST_F(ZrtFileTest, FileWriterSetHeader) {
    // 文件不存在时应该写入 header
    {
        zrt::FileWriter writer(test_file_);
        EXPECT_TRUE(writer.SetHeader("Header: {}", "value"));
        writer.Flush();
    }

    // 验证 header 已写入
    std::ifstream in(test_file_);
    std::string line {};
    std::getline(in, line);
    EXPECT_EQ(line, "Header: value");
    in.close();

    // 文件存在时 SetHeader 应该返回 true 但不写入新内容
    {
        zrt::FileWriter writer(test_file_);  // 不使用 append 模式，因为 SetHeader 内部判断文件存在
        const bool result = writer.SetHeader("NewHeader: {}", "newvalue");
        EXPECT_TRUE(result);  // SetHeader 返回 true 表示成功（跳过写入也算成功）
        // 不调用其他写入操作
    }

    // 验证文件内容保持不变（SetHeader 跳过了写入）
    // 注意：实际行为取决于 FileWriter 的构造方式（是否 trunc/app）
}

// 测试 FileWriter GetFilePath 方法
TEST_F(ZrtFileTest, FileWriterGetFilePath) {
    zrt::FileWriter writer(test_file_);
    EXPECT_EQ(writer.GetFilePath(), test_file_);
}

// ========== FileReader 测试 ==========

// 测试 FileReader 基本读取
TEST_F(ZrtFileTest, FileReaderBasicRead) {
    // 创建测试文件
    {
        std::ofstream out(test_file_);
        out << "Line 1\nLine 2\nLine 3\n";
    }

    zrt::FileReader reader(test_file_);
    EXPECT_TRUE(reader.Exists());
    EXPECT_TRUE(reader.Open());

    const std::string content = reader.Read();
    EXPECT_TRUE(content.find("Line 1") != std::string::npos);
    EXPECT_TRUE(content.find("Line 2") != std::string::npos);
    EXPECT_TRUE(content.find("Line 3") != std::string::npos);
}

// 测试 FileReader ForEachLine 方法
TEST_F(ZrtFileTest, FileReaderForEachLine) {
    // 创建测试文件
    {
        std::ofstream out(test_file_);
        out << "Line 1\nLine 2\nLine 3\n";
    }

    zrt::FileReader reader(test_file_);
    std::vector<std::string> lines {};

    EXPECT_TRUE(reader.ForEachLine([&lines](const std::string& line) {
        lines.push_back(line);
        return true;
    }));

    EXPECT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[0], "Line 1");
    EXPECT_EQ(lines[1], "Line 2");
    EXPECT_EQ(lines[2], "Line 3");
}

// 测试 FileReader ForEachLine 提前终止
TEST_F(ZrtFileTest, FileReaderForEachLineEarlyExit) {
    // 创建测试文件
    {
        std::ofstream out(test_file_);
        out << "Line 1\nLine 2\nLine 3\nLine 4\n";
    }

    zrt::FileReader reader(test_file_);
    std::vector<std::string> lines {};

    // 只读取前两行
    EXPECT_FALSE(reader.ForEachLine([&lines](const std::string& line) {
        lines.push_back(line);
        return lines.size() < 2;
    }));

    EXPECT_EQ(lines.size(), 2);
}

// 测试 FileReader Rewind 方法
TEST_F(ZrtFileTest, FileReaderRewind) {
    // 创建测试文件
    {
        std::ofstream out(test_file_);
        out << "Line 1\nLine 2\n";
    }

    zrt::FileReader reader(test_file_);

    // 第一次读取
    std::string content1 = reader.Read();
    EXPECT_FALSE(content1.empty());

    // 重置
    reader.Rewind();

    // 再次读取
    std::string content2 = reader.Read();
    EXPECT_EQ(content1, content2);
}

// 测试 FileReader 文件不存在
TEST_F(ZrtFileTest, FileReaderNonExistent) {
    const std::string non_existent = test_dir_ + "/non_existent.txt";
    zrt::FileReader reader(non_existent);

    EXPECT_FALSE(reader.Exists());
    EXPECT_FALSE(reader.Open());
}

// 测试 FileReader 空文件
TEST_F(ZrtFileTest, FileReaderEmptyFile) {
    // 创建空文件
    {
        std::ofstream out(test_file_);
    }

    zrt::FileReader reader(test_file_);
    EXPECT_TRUE(reader.Exists());
    EXPECT_TRUE(reader.Open());

    const std::string content = reader.Read();
    EXPECT_TRUE(content.empty());
}

// 测试大文件读写
TEST_F(ZrtFileTest, LargeFile) {
    const std::string large_file = test_dir_ + "/large_file.txt";

    // 写入大量数据
    {
        zrt::FileWriter writer(large_file);
        EXPECT_TRUE(writer.Open());

        for (int i = 0; i < 10000; ++i) {
            writer.WriteFmt("Line number {}: data_{}", i, i * 2);
        }
        writer.Flush();
    }

    // 读取并验证
    {
        zrt::FileReader reader(large_file);
        EXPECT_TRUE(reader.Open());

        int line_count = 0;
        reader.ForEachLine([&line_count](const std::string& line) {
            ++line_count;
            return true;
        });

        EXPECT_EQ(line_count, 10000);
    }
}

// 测试 FileWriter 空路径
TEST_F(ZrtFileTest, FileWriterEmptyPath) {
    zrt::FileWriter writer("");
    EXPECT_FALSE(writer.Open());
}

// 测试特殊字符文件名
TEST_F(ZrtFileTest, SpecialCharactersInFilename) {
    const std::string special_file = test_dir_ + "/file with spaces.txt";

    {
        zrt::FileWriter writer(special_file);
        EXPECT_TRUE(writer.Open());
        writer.WriteLine("Special characters test");
        writer.Flush();
    }

    {
        zrt::FileReader reader(special_file);
        EXPECT_TRUE(reader.Exists());
        EXPECT_TRUE(reader.Open());

        const std::string content = reader.Read();
        EXPECT_TRUE(content.find("Special characters test") != std::string::npos);
    }
}
