#ifndef NAIVE_ALLOCATOR_HPP
#define NAIVE_ALLOCATOR_HPP

#include <climits>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>

// normal allocator interface
// using malloc and free only
template <class T>
class Nallocator
{
public:
    typedef T value_type;
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;

    Nallocator() = default;

    template <class U>
    Nallocator(const Nallocator<U> &other) noexcept;

    T *allocate(size_type n)
    {
        auto buf = (T *)(::operator new((size_type)(n * sizeof(T))));
        if (buf == 0)
            throw std::bad_alloc();
        return buf;
    }

    void deallocate(T *buf, size_type) { ::operator delete(buf); }
};

#endif