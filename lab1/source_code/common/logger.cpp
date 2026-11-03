// =============================================================================
//  logger.cpp  --  线程安全 UTF-8 日志文件实现
// =============================================================================
#include "logger.h"
#include "text.h"

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include <vector>

namespace common {

FileLogger::~FileLogger() { Close(); }

bool FileLogger::Open(const std::wstring& path) {
    Close();
    if (path.empty()) {
        return false;
    }
    // 用 _wfsopen + _SH_DENYNO：允许别的程序（记事本、VS Code）
    FILE* file = _wfsopen(path.c_str(), L"wb", _SH_DENYNO);
    if (file == nullptr) {
        return false;
    }
    // 写入 UTF-8 BOM
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    std::fwrite(bom, 1, sizeof(bom), file);
    std::fflush(file);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_file = file;
    m_path = path;
    return true;
}

void FileLogger::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file != nullptr) {
        std::fclose(m_file);
        m_file = nullptr;
    }
}

void FileLogger::Write(const std::wstring& line) {
    const std::string utf8 = text::WideToUtf8(line);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file == nullptr) {
        return;
    }
    std::fwrite(utf8.data(), 1, utf8.size(), m_file);
    std::fwrite("\r\n", 1, 2, m_file);
    std::fflush(m_file); 
}

std::wstring ExeDirectory() {
    wchar_t buffer[MAX_PATH] = {0};
    const DWORD n = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer, n);
    const size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        path.erase(pos);
    }
    return path;
}

}  // namespace common
