// =============================================================================
//  logger.h  --  简单的线程安全 UTF-8 日志文件
//  服务端在 exe 同级目录下生成 server_run.log，记录全部交互过程
// =============================================================================
#pragma once

#include "platform.h"

#include <mutex>
#include <string>

namespace common {

class FileLogger {
public:
    FileLogger() = default;
    ~FileLogger();
    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

    // 打开日志文件
    bool Open(const std::wstring& path);
    void Close();
    bool IsOpen() const { return m_file != nullptr; }
    std::wstring Path() const { return m_path; }

    // 写入一行（自动补时间戳与换行），线程安全
    void Write(const std::wstring& line);

private:
    std::mutex   m_mutex;
    FILE*        m_file = nullptr;
    std::wstring m_path;
};

// 取得 exe 所在目录
std::wstring ExeDirectory();

}  // namespace common
