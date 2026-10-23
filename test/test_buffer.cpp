#include "catch.hpp"
#include "uring_service.h"
#include "buffer/buffer_registration.h"
#include "buffer/buffer_provide.h"
#include <utils/buffer.h>

TEST_CASE("BufferRegistration basic acquire and release", "[buffer_registration]") {
    using namespace eular;

    UringService service(64);
    REQUIRE(service.isValid());

    // Prepare buffers
    std::vector<ByteBuffer> buffers;
    for (int i = 0; i < 4; ++i) {
        ByteBuffer buf(1024);
        buffers.push_back(std::move(buf));
    }

    BufferRegistration reg(buffers, &service);

    // Acquire buffers
    BufferId* b0 = reg.acquire();
    REQUIRE(b0 != nullptr);
    REQUIRE(b0->isRegistrationBuffer());
    REQUIRE(b0->bid() == 0);

    BufferId* b1 = reg.acquire();
    REQUIRE(b1 != nullptr);
    REQUIRE(b1->bid() == 1);

    // Release b0 and acquire again
    reg.release(b0);
    BufferId* b_reacquired = reg.acquire();
    REQUIRE(b_reacquired != nullptr);
    REQUIRE(b_reacquired->bid() == 0);
}

TEST_CASE("BufferProvide basic acquire and release in coroutine", "[buffer_provide]") {
    using namespace eular;

    UringService service(64);
    REQUIRE(service.isValid());

    service.schedule([&]() noexcept {
        ByteBuffer buf(2048);
        int32_t gid = 1;
        BufferProvide provider(buf, &service, gid);

        BufferId* id = provider.acquire();
        REQUIRE(id != nullptr);
        REQUIRE(id->gid() == gid);
        REQUIRE(id->size() == 2048);

        // While in use, cannot acquire again
        REQUIRE(provider.acquire() == nullptr);

        provider.release(id);
        service.stop();
    });

    service.start();
}

#include "buffer/buffer.h"
#include "io/co_file.h"
#include <unistd.h>
#include <cstring>

TEST_CASE("BufferRegistration with co_read_fixed and co_write_fixed", "[buffer_fixed_io]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    service.schedule([&]() noexcept {
        std::vector<ByteBuffer> buffers;
        ByteBuffer b0(1024);
        std::memcpy(b0.data(), "hello registered buffer", 23);
        b0.resize(23);
        buffers.push_back(std::move(b0));

        BufferRegistration reg(buffers, &service);
        BufferId* bid = reg.acquire();
        REQUIRE(bid != nullptr);

        char tmpPath[] = "/tmp/couring_fixed_test_XXXXXX";
        int fd = mkstemp(tmpPath);
        REQUIRE(fd >= 0);

        // Write with registered buffer
        ssize_t w = co_write_fixed(fd, bid, 23, 0);
        REQUIRE(w == 23);

        // Clear memory in buffer
        std::memset(bid->buffer(), 0, 23);

        // Read back with registered buffer
        ssize_t r = co_read_fixed(fd, bid, 23, 0);
        REQUIRE(r == 23);
        REQUIRE(std::memcmp(bid->buffer(), "hello registered buffer", 23) == 0);

        reg.release(bid);
        close(fd);
        unlink(tmpPath);
        service.stop();
    });

    service.start();
}

