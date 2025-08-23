#include "ResultSet.h"
import <stdexcept>;

ResultSet::ResultSet(std::vector<std::string> columns) 
    : columnNames(std::move(columns)) {}

void ResultSet::addRow(const Row& row) {
    rows.push_back(row);
}

const ResultSet::Row& ResultSet::getRow(size_t index) const {
    if (index >= rows.size()) {
        throw std::out_of_range("Row index out of range");
    }
    return rows[index];
}

const ResultSet::Value& ResultSet::getValue(const std::string& columnName) const {
    if (currentRowIndex >= rows.size()) {
        throw std::out_of_range("No current row");
    }
    
    const auto& row = rows[currentRowIndex];
    auto it = row.find(columnName);
    if (it == row.end()) {
        throw std::out_of_range("Column name not found: " + columnName);
    }
    
    return it->second;
}

const ResultSet::Value& ResultSet::getValue(size_t columnIndex) const {
    if (currentRowIndex >= rows.size()) {
        throw std::out_of_range("No current row");
    }
    
    if (columnIndex >= columnNames.size()) {
        throw std::out_of_range("Column index out of range");
    }
    
    const auto& columnName = columnNames[columnIndex];
    return getValue(columnName);
}

bool ResultSet::next() {
    if (currentRowIndex < rows.size()) {
        currentRowIndex++;
    }
    
    return currentRowIndex < rows.size();
}

void ResultSet::beforeFirst() {
    currentRowIndex = 0;
}
