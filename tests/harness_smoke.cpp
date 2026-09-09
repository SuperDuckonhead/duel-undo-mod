#include "test_support.h"

#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--intentional-failure") {
        try {
            CHECK(false);
        } catch (const std::runtime_error& error) {
            return std::string_view(error.what()) == "false" ? 1 : 0;
        }
        return 0;
    }

    CHECK(argc == 1);
    CHECK(true);
    return 0;
}
