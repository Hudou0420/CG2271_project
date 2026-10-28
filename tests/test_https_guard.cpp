#include "https_guard.h"
#include <atomic>
#include <cassert>
#include <thread>
#include <cstdio>
int main() {
    https_guard_init();
    {
        HttpsLease cloud;
        assert(cloud.acquired());
        std::thread ai([] {
            HttpsLease wait(20);
            assert(!wait.acquired()); // cannot start TLS while Firebase holds it
        });
        ai.join();
    }
    {
        HttpsLease afterFailure(0);
        assert(afterFailure.acquired()); // destructor released it on prior scope exit
    }
    std::atomic<int> liveTls{0}, requests{0};
    auto traffic = [&] {
        for (int i = 0; i < 100; ++i) {
            HttpsLease lease;
            assert(lease.acquired());
            assert(liveTls.fetch_add(1) == 0);
            std::this_thread::yield();
            assert(liveTls.fetch_sub(1) == 1);
            ++requests;
        }
    };
    std::thread firebase(traffic), gemini(traffic);
    firebase.join(); gemini.join();
    assert(liveTls == 0 && requests == 200);
    puts("HTTPS clients never overlap; timeouts and scope exits release the lease");
}
