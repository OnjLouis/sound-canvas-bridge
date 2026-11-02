#include "Pipeline.h"
#include <iostream>
#include <stdexcept>
int main() {
    scbridge::WakeSignal signal;
    if(!signal.valid()) return 1;
    const auto limit=[] { return std::chrono::steady_clock::now()+std::chrono::milliseconds(100); };
    for(int i=0;i<1000;++i) { signal.notify();signal.notify();if(!signal.waitUntil(limit())) return 2; }
    signal.notify();signal.reset();
    if(signal.waitUntil(std::chrono::steady_clock::now()+std::chrono::milliseconds(2))) return 3;
    std::thread producer([&] { std::this_thread::sleep_for(std::chrono::milliseconds(5));signal.notify(); });
    const bool woke=signal.waitUntil(limit());producer.join();
    if(!woke) return 4;
    std::cout<<"Coalesced notifications, reset, timeout and cross-thread wake passed\n";
}
