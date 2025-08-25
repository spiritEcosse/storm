#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <memory>
#include <functional>

/**
 * @brief Structured logging system for database operations
 *
 * This class provides a thread-safe logging system with multiple
 * output targets and severity levels.
 */
class Logger {
  public:
    enum class Level { DEBUG, INFO, WARNING, ERROR, CRITICAL };

  private:
    static std::shared_ptr<Logger> instance;
    static std::mutex              instanceMutex;

    std::mutex    logMutex;
    Level         minLevel = Level::INFO;
    std::ofstream fileStream;
    bool          consoleOutput = true;

    /**
     * @brief Convert log level to string
     * @param level Log level
     * @return String representation of log level
     */
    static std::string levelToString(Level level) {
        switch (level) {
        case Level::DEBUG:
            return "DEBUG";
        case Level::INFO:
            return "INFO";
        case Level::WARNING:
            return "WARNING";
        case Level::ERROR:
            return "ERROR";
        case Level::CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * @brief Get current timestamp as string
     * @return Current timestamp formatted as string
     */
    static std::string getCurrentTimestamp() {
        auto now  = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();

        return ss.str();
    }

    /**
     * @brief Private constructor for singleton pattern
     */
    Logger() = default;

  public:
    /**
     * @brief Get the singleton instance
     * @return Shared pointer to logger instance
     */
    static std::shared_ptr<Logger> getInstance() {
        std::lock_guard<std::mutex> lock(instanceMutex);
        if (!instance) {
            instance = std::shared_ptr<Logger>(new Logger());
        }
        return instance;
    }

    /**
     * @brief Set the minimum log level
     * @param level Minimum log level to output
     */
    void setLevel(Level level) {
        std::lock_guard<std::mutex> lock(logMutex);
        minLevel = level;
    }

    /**
     * @brief Enable or disable console output
     * @param enable True to enable console output, false to disable
     */
    void enableConsoleOutput(bool enable) {
        std::lock_guard<std::mutex> lock(logMutex);
        consoleOutput = enable;
    }

    /**
     * @brief Set the log file
     * @param filePath Path to log file
     * @return True if file opened successfully, false otherwise
     */
    bool setLogFile(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(logMutex);

        if (fileStream.is_open()) {
            fileStream.close();
        }

        // Create directory if it doesn't exist
        auto path = std::filesystem::path(filePath).parent_path();
        if (!path.empty()) {
            std::filesystem::create_directories(path);
        }

        fileStream.open(filePath, std::ios::app);
        return fileStream.is_open();
    }

    /**
     * @brief Log a message
     * @param level Log level
     * @param message Message to log
     */
    void log(Level level, const std::string& message) {
        if (level < minLevel) {
            return;
        }

        std::lock_guard<std::mutex> lock(logMutex);

        std::string timestamp        = getCurrentTimestamp();
        std::string levelStr         = levelToString(level);
        std::string formattedMessage = timestamp + " [" + levelStr + "] " + message;

        if (consoleOutput) {
            // Choose output stream based on level
            auto& stream = (level >= Level::WARNING) ? std::cerr : std::cout;
            stream << formattedMessage << std::endl;
        }

        if (fileStream.is_open()) {
            fileStream << formattedMessage << std::endl;
            fileStream.flush();
        }
    }

    /**
     * @brief Log a debug message
     * @param message Message to log
     */
    void debug(const std::string& message) {
        log(Level::DEBUG, message);
    }

    /**
     * @brief Log an info message
     * @param message Message to log
     */
    void info(const std::string& message) {
        log(Level::INFO, message);
    }

    /**
     * @brief Log a warning message
     * @param message Message to log
     */
    void warning(const std::string& message) {
        log(Level::WARNING, message);
    }

    /**
     * @brief Log an error message
     * @param message Message to log
     */
    void error(const std::string& message) {
        log(Level::ERROR, message);
    }

    /**
     * @brief Log a critical message
     * @param message Message to log
     */
    void critical(const std::string& message) {
        log(Level::CRITICAL, message);
    }
};

// Initialize static members
std::shared_ptr<Logger> Logger::instance = nullptr;
std::mutex              Logger::instanceMutex;
