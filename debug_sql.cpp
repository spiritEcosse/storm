#include "src/QuerySet.h"
#include <iostream>

// Simple model classes for testing
struct Author {
    int id = 0;
    std::string name;
    int age;
    std::string email;
    bool is_active = true;
    double rating = 0.0;
    float score = 0.0;
    std::string middleName;
    std::string biography;
    
    Author() = default;
    Author(const std::string& n, int a, const std::string& e) 
        : name(n), age(a), email(e) {}
};

// Add reflection for Author
REFL_AUTO(
    type(Author),
    field(id),
    field(name),
    field(age),
    field(email),
    field(is_active),
    field(rating),
    field(score),
    field(middleName),
    field(biography)
)

int main() {
    // Create a connection to an in-memory SQLite database
    auto conn = std::make_shared<storm::Connection>(":memory:");
    
    // Create a test author
    Author testAuthor("Test Author", 40, "test@example.com");
    
    // Use QuerySet to generate SQL
    auto stmt = storm::QuerySet<Author>(conn).stmt_insert(testAuthor);
    
    // Print the generated SQL
    std::string sql = stmt.sql();
    std::cout << "Generated SQL: " << sql << std::endl;
    
    return 0;
}
