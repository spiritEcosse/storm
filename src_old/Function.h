#pragma once

namespace orm {

    class Function final : public BaseClass {
    public:
        using BaseClass::BaseClass;

        explicit Function(std::string function) : function(std::move(function)) {}

        [[nodiscard]] std::string toStr() const {
            return function;
        }

        [[nodiscard]] std::string getFullFieldName() const {
            return function;
        }

        std::string function;
    };
}