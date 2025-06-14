# Database configuration from JSON file
# This script reads database configuration from a JSON file instead of environment variables
message(STATUS "Reading database configuration from JSON file: ${CMAKE_CURRENT_SOURCE_DIR}/config.json")

# Path to the JSON config file - use a relative path from the project root
set(CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config.json")

# Check if the config file exists
if(NOT EXISTS "${CONFIG_FILE}")
    message(FATAL_ERROR "Configuration file not found: ${CONFIG_FILE}")
endif()

# Read the JSON file content
file(READ "${CONFIG_FILE}" CONFIG_CONTENT)

# Use a simpler approach with file(STRINGS) to parse the JSON file
# Extract key-value pairs using regex matching
set(DB_VARS
    DB_DRIVER
    DB_HOST
    DB_PORT
    DB_DATABASE
    DB_USERNAME
    DB_PASSWORD
)

# Process each variable
foreach(VAR ${DB_VARS})
    # Extract the value using regex
    string(REGEX MATCH "\"${VAR}\"[ \t]*:[ \t]*\"([^\"]*)\"" MATCH_RESULT "${CONFIG_CONTENT}")
    
    if(MATCH_RESULT)
        # Extract the captured group (the value)
        string(REGEX REPLACE "\"${VAR}\"[ \t]*:[ \t]*\"([^\"]*)\"" "\\1" ${VAR} "${MATCH_RESULT}")
    else()
        message(FATAL_ERROR "${VAR} not found in config.json")
    endif()
    
    # Check if the value is empty
    if("${${VAR}}" STREQUAL "")
        message(FATAL_ERROR "${VAR} value is empty in config.json")
    endif()
    
    # Print the variable value
    message(STATUS "${VAR} = ${${VAR}}")
endforeach()

# Define the variables as compile definitions
target_compile_definitions(${PROJECT_NAME} PUBLIC
    -DDB_DRIVER="${DB_DRIVER}"
    -DDB_HOST="${DB_HOST}"
    -DDB_PORT="${DB_PORT}"
    -DDB_DATABASE="${DB_DATABASE}"
    -DDB_USERNAME="${DB_USERNAME}"
    -DDB_PASSWORD="${DB_PASSWORD}"
)

message(STATUS "Database configuration loaded successfully from config.json")
