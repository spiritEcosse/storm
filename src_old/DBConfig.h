#pragma once

#include <string>
#include <sstream>
#include <chrono>
#include <iostream>

#include "envs.h"

struct DBConfig {
    std::string host;
    uint16_t    port;
    std::string database;
    std::string username;
    std::string password;
    std::string driver;

    size_t               maxConnections = 10;
    std::chrono::seconds connectionTimeout{30};
    std::chrono::seconds idleTimeout{300};

    DBConfig() : host(DB_HOST), database(DB_DATABASE), username(DB_USERNAME), password(DB_PASSWORD), driver(DB_DRIVER) {
        try {
            port = static_cast<uint16_t>(std::stoi(DB_PORT));
        } catch (const std::exception& e) {
            std::cerr << "Error: DB_PORT is not a valid number: " << e.what() << std::endl;

            if (driver == "postgresql") {
                port = 5432;
            } else if (driver == "mysql") {
                port = 3306;
            } else {
                throw; // Re-throw the exception
            }
        }
    }

    [[nodiscard]] std::string connectionString() const {
        std::ostringstream connStr;

        if (driver == "postgresql") {
            connStr << "host=" << host << " port=" << port << " dbname=" << database << " user=" << username
                    << " password=" << password;
        } else if (driver == "mysql") {
            connStr << "host=" << host << ";port=" << port << ";database=" << database << ";user=" << username
                    << ";password=" << password;
        }

        return connStr.str();
    }
};
