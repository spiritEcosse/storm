import <string>;
import <format>;
import <iostream>;
int main() {
    std::string s = "Hello";
    std::cout << std::format("{}", s) << std::endl;
}
clang++ - std = c++ 23 - stdlib = libc++ - fmodules test.cpp
