//
// Created by AI Assistant on 2025-07-09.
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/file.h"
#include <filesystem>
#include <fstream>

class FileTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = "/tmp/zrtools_test";
        test_file = test_dir + "/test_file.txt";
        
        // 创建测试目录
        std::filesystem::create_directories(test_dir);
    }
    
    void TearDown() override {
        // 清理测试文件和目录
        std::filesystem::remove_all(test_dir);
    }
    
    std::string test_dir;
    std::string test_file;
};

// 测试File类基本功能
TEST_F(FileTest, BasicFileOperations) {
    // 创建文件并写入内容
    {
        zrt::File file(test_file, std::ios::out);
        EXPECT_TRUE(file.isOpen());
        EXPECT_TRUE(file.write("Hello, World!"));
    }
    
    // 读取文件内容
    {
        zrt::File file(test_file, std::ios::in);
        EXPECT_TRUE(file.isOpen());
        
        std::string content;
        EXPECT_TRUE(file.read(content));
        EXPECT_EQ(content, "Hello, World!\n");  // 文件可能包含换行符
    }
    
    // 使用read()方法直接返回内容
    {
        zrt::File file(test_file, std::ios::in);
        EXPECT_TRUE(file.isOpen());
        
        std::string content = file.read();
        EXPECT_EQ(content, "Hello, World!\n");  // 文件可能包含换行符
    }
}

// 测试文件读写模式
TEST_F(FileTest, FileReadWriteModes) {
    // 测试写入模式
    {
        zrt::File file(test_file, std::ios::out);
        EXPECT_TRUE(file.isOpen());
        EXPECT_TRUE(file.write("Initial content"));
    }
    
    // 测试追加模式
    {
        zrt::File file(test_file, std::ios::app);
        EXPECT_TRUE(file.isOpen());
        EXPECT_TRUE(file.write("\nAppended content"));
    }
    
    // 验证内容
    {
        zrt::File file(test_file, std::ios::in);
        EXPECT_TRUE(file.isOpen());
        
        std::string content = file.read();
        EXPECT_TRUE(content.find("Initial content") != std::string::npos);
        EXPECT_TRUE(content.find("Appended content") != std::string::npos);
    }
}

// 测试文件不存在的情况
TEST_F(FileTest, NonExistentFile) {
    std::string non_existent = test_dir + "/non_existent.txt";
    
    // 尝试读取不存在的文件
    {
        EXPECT_THROW(zrt::File file(non_existent, std::ios::in), std::runtime_error);
    }
    
    // 创建不存在的文件
    {
        zrt::File file(non_existent, std::ios::out);
        EXPECT_TRUE(file.isOpen());
        EXPECT_TRUE(file.write("Created file"));
    }
    
    // 验证文件已创建
    {
        zrt::File file(non_existent, std::ios::in);
        EXPECT_TRUE(file.isOpen());
        
        std::string content = file.read();
        EXPECT_EQ(content, std::string("Created file\n"));
    }
}

// 测试FileWriter类基本功能
TEST_F(FileTest, FileWriterBasic) {
    std::string writer_file = test_dir + "/writer_test.txt";
    
    {
        zrt::FileWriter writer(writer_file);
        
        EXPECT_TRUE(writer.WriteLine("Line 1"));
        EXPECT_TRUE(writer.WriteLine("Line 2"));
        EXPECT_TRUE(writer.WriteLine("Line 3"));
        
        writer.Flush();
    }
    
    // 验证写入的内容
    std::ifstream file(writer_file);
    EXPECT_TRUE(file.is_open());
    
    std::string line;
    std::getline(file, line);
    EXPECT_EQ(line, "Line 1");
    
    std::getline(file, line);
    EXPECT_EQ(line, "Line 2");
    
    std::getline(file, line);
    EXPECT_EQ(line, "Line 3");
}

// 测试FileWriter的Open方法
TEST_F(FileTest, FileWriterOpenMethod) {
    std::string writer_file = test_dir + "/writer_open_test.txt";
    
    {
        zrt::FileWriter writer;
        EXPECT_TRUE(writer.Open(writer_file));
        
        EXPECT_TRUE(writer.WriteLine("Opened file"));
        EXPECT_TRUE(writer.WriteLine("Second line"));
        
        writer.Flush();
    }
    
    // 验证内容
    std::ifstream file(writer_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    EXPECT_TRUE(content.find("Opened file") != std::string::npos);
    EXPECT_TRUE(content.find("Second line") != std::string::npos);
}

// 测试FileWriter的格式化写入
TEST_F(FileTest, FileWriterFormatted) {
    std::string writer_file = test_dir + "/writer_format_test.txt";
    
    {
        zrt::FileWriter writer(writer_file);
        
        EXPECT_TRUE(writer.WriteFmt("Number: {}", 42));
        EXPECT_TRUE(writer.WriteFmt("String: {}", "test"));
        EXPECT_TRUE(writer.WriteFmt("Float: {:.2f}", 3.14159));
        
        writer.Flush();
    }
    
    // 验证格式化内容
    std::ifstream file(writer_file);
    std::string line;
    
    std::getline(file, line);
    EXPECT_EQ(line, "Number: 42");
    
    std::getline(file, line);
    EXPECT_EQ(line, "String: test");
    
    std::getline(file, line);
    EXPECT_EQ(line, "Float: 3.14");
}

// 测试错误情况
TEST_F(FileTest, ErrorHandling) {
    // 测试无效路径
    std::string invalid_path = "/invalid/path/file.txt";
    
    {
        EXPECT_THROW(zrt::File file(invalid_path, std::ios::out), std::runtime_error);
    }
    
    // 测试FileWriter无效路径
    {
        zrt::FileWriter writer;
        EXPECT_FALSE(writer.Open(invalid_path));
        EXPECT_FALSE(writer.WriteLine("Should fail"));
    }
}

// 测试大文件处理
TEST_F(FileTest, LargeFileHandling) {
    std::string large_file = test_dir + "/large_file.txt";
    
    // 创建大文件
    {
        zrt::FileWriter writer(large_file);
        
        for (int i = 0; i < 1000; ++i) {
            writer.WriteFmt("This is line number {}", i);
        }
        
        writer.Flush();
    }
    
    // 读取大文件
    {
        zrt::File file(large_file, std::ios::in);
        EXPECT_TRUE(file.isOpen());
        
        std::string content = file.read();
        EXPECT_TRUE(content.find("This is line number 0") != std::string::npos);
        EXPECT_TRUE(content.find("This is line number 999") != std::string::npos);
    }
}

// 测试文件覆盖
TEST_F(FileTest, FileOverwrite) {
    std::string overwrite_file = test_dir + "/overwrite_test.txt";
    
    // 第一次写入
    {
        zrt::FileWriter writer(overwrite_file);
        writer.WriteLine("Original content");
        writer.Flush();
    }
    
    // 覆盖写入
    {
        zrt::FileWriter writer(overwrite_file);
        writer.WriteLine("New content");
        writer.Flush();
    }
    
    // 验证内容被覆盖
    {
        zrt::File file(overwrite_file, std::ios::in);
        std::string content = file.read();
        EXPECT_EQ(content, "New content\n");
        EXPECT_TRUE(content.find("Original content") == std::string::npos);
    }
}

// 测试空文件处理
TEST_F(FileTest, EmptyFileHandling) {
    std::string empty_file = test_dir + "/empty_file.txt";
    
    // 创建空文件
    {
        zrt::File file(empty_file, std::ios::out);
        EXPECT_TRUE(file.isOpen());
        EXPECT_TRUE(file.write(""));
    }
    
    // 读取空文件
    {
        zrt::File file(empty_file, std::ios::in);
        EXPECT_TRUE(file.isOpen());
        
        std::string content = file.read();
        EXPECT_TRUE(content.empty());
    }
}

// 测试多次打开关闭
TEST_F(FileTest, MultipleOpenClose) {
    std::string multi_file = test_dir + "/multi_test.txt";
    
    // 多次创建FileWriter对象
    for (int i = 0; i < 5; ++i) {
        zrt::FileWriter writer(multi_file);
        writer.WriteFmt("Iteration: {}", i);
        writer.Flush();
        // writer自动销毁，文件关闭
    }
    
    // 验证最后一次写入
    {
        zrt::File file(multi_file, std::ios::in);
        std::string content = file.read();
        EXPECT_EQ(content, "Iteration: 4\n");
    }
}