
#pragma once
#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

class Logger {
public:
    enum class Level {
        Info,
        Warning,
        Error,
        Debug
    };

    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void info(const std::wstring& msg)    { log(Level::Info, msg); }
    void warning(const std::wstring& msg) { log(Level::Warning, msg); }
    void error(const std::wstring& msg)   { log(Level::Error, msg); }
    void debug(const std::wstring& msg)   { log(Level::Debug, msg); }

    void setLogFile(const std::wstring& filename) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logFile = filename;
        openLogFile();
    }

    void setLevel(Level level) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_level = level;
    }

private:
    Logger() : m_logFile(L"ms2000_log.txt"), m_level(Level::Info) {
        openLogFile();
    }
    ~Logger() {
        if (m_ofs.is_open()) m_ofs.close();
    }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void openLogFile() {
        if (m_ofs.is_open()) m_ofs.close();
        // Simple log rotation: if file > 5MB, rename to .bak
        namespace fs = std::filesystem;
        try {
            if (fs::exists(m_logFile) && fs::file_size(m_logFile) > 5 * 1024 * 1024) {
                fs::rename(m_logFile, m_logFile + L".bak");
            }
        } catch (...) {}
        m_ofs.open(m_logFile, std::ios::app);
    }

    void log(Level level, const std::wstring& msg) {
        if (level > m_level) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_ofs.is_open()) openLogFile();
        m_ofs << timestamp() << L" [" << levelToString(level) << L"] " << msg << std::endl;
    }

    std::wstring timestamp() {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto itt = system_clock::to_time_t(now);
        std::wstringstream ss;
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &itt);
#else
        localtime_r(&itt, &tm_buf);
#endif
        ss << std::put_time(&tm_buf, L"%Y-%m-%d %H:%M:%S");
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        ss << L"." << std::setfill(L'0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::wstring levelToString(Level level) {
        switch (level) {
            case Level::Info: return L"INFO";
            case Level::Warning: return L"WARN";
            case Level::Error: return L"ERR";
            case Level::Debug: return L"DBG";
            default: return L"UNK";
        }
    }

    std::wstring m_logFile;
    std::wofstream m_ofs;
    std::mutex m_mutex;
    Level m_level;
};
