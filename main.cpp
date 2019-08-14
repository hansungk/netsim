#include "router.h"
#include <iostream>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s EXEC-FILE\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Router router;
    router.put(Flit{0});
    router.put(Flit{1});
    router.put(Flit{2});

    router.run();

    std::cout << "out_buf len: " << router.out_buf.size() << std::endl;

    return 0;
}
