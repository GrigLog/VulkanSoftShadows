#include "src/App.h"

#include <cstdlib>
#include <exception>
#include <iostream>


int main() {
    try {
        App app;
        app.run();
        return 0;
    } catch (const std::exception& exceptionValue) {
        std::cerr << "Unhandled exception: " << exceptionValue.what() << std::endl;
        return 1;
    }
}
