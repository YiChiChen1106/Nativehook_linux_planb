#include <cstdlib>
#include <cstdio>

__attribute__((noinline)) void LeakPathA()
{
    void* ptr = std::malloc(64);
    std::printf("%p\n", ptr);
}

__attribute__((noinline)) void FreePathB()
{
    void* ptr = std::malloc(32);
    std::free(ptr);
}

int main()
{
    LeakPathA();
    FreePathB();
    return 0;
}
