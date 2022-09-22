#ifndef MYALLOCATOR_HPP
#define MYALLOCATOR_HPP

#include <climits>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>

class pool_alloc_base
{
protected:
    typedef std::size_t size_t;
    // the max size of small block in memory pool
    enum
    {
        MAX_BYTES = 65536
    };
    // the align size of each block, i.e. the size could be divided by 64
    enum
    {
        ALIGN = 64
    };
    // the number of free lists
    enum
    {
        FREE_LIST_SIZE = (size_t)MAX_BYTES / (size_t)ALIGN
    };

    // the union object of free list
    union obj
    {
        union obj *free_list_link; // 下一个内存块节点
        char client_data[1];       // 用户看到的内存地址
    };
    static obj *volatile free_list[FREE_LIST_SIZE]; //链表头指针数组

    // 维护内存块状态
    static char *start_mem_pool; // 内存池中可用空间的起始地址
    static char *end_mem_pool;   //内存池中可用空间的结束地址
    static size_t heap_size;     //已经从堆中申请的总空间大小

    // align the block
    inline size_t round_up(size_t bytes)
    {
        return (((bytes) + (size_t)ALIGN - 1) & ~((size_t)ALIGN - 1));
    }

    // get the header of the linked list according to bytes
    obj *volatile *get_free_list(size_t bytes) throw()
    {
        size_t i = (bytes + (size_t)ALIGN - 1) / (size_t)ALIGN - 1;
        return free_list + i;
    }

    // call refill when free_list has no enough resource
    void *refill(size_t n)
    {
        int cnt_objs = 20; // try to allocate 20 blocks of size n
        char *chunk = allocate_chunk(n, cnt_objs);
        obj *volatile *my_free_list;
        obj *result;
        obj *current_obj;
        obj *next_obj;
        // if memory pool allocates exactly one block of size n in allocate_chunk, we get what we want and return
        if (cnt_objs == 1)
            return chunk;
        // return first n space allocated by allocate_chunk and store the rest in the list
        my_free_list = get_free_list(n);
        result = (obj *)(void *)chunk;
        *my_free_list = next_obj = (obj *)(void *)(chunk + n);
        for (int i = 1;; i++)
        {
            current_obj = next_obj;
            next_obj = (obj *)(void *)((char *)next_obj + n);
            if (cnt_objs - 1 == i)
            {
                current_obj->free_list_link = nullptr;
                break;
            }
            else
                current_obj->free_list_link = next_obj;
        }
        return result;
    }

    /**
     * Allocate memory block from memory pool to avoid fragmentation
     * @param size size of each object
     * @param cnt_objs number of objects
     */
    char *allocate_chunk(size_t size, int &cnt_objs)
    {
        char *result;
        size_t total_bytes = size * cnt_objs;              // space needed
        size_t bytes_left = end_mem_pool - start_mem_pool; // free space in current block
        if (bytes_left >= total_bytes)
        {
            // if the space left is enough, allocate and update start address of the free block
            result = start_mem_pool;
            start_mem_pool += total_bytes;
            return result;
        }
        else if (bytes_left >= size)
        {
            // if the space left is not enough, but enough to allocate at least one object
            // calculate space needed to allocate objects
            cnt_objs = (int)(bytes_left / size);
            total_bytes = size * cnt_objs;
            result = start_mem_pool;
            start_mem_pool += total_bytes;
            return result;
        }
        else
        {
            if (bytes_left > 0 && start_mem_pool != nullptr)
            {
                // make full use of the space left by putting them into linked list of array free_list
                obj *volatile *my_free_list = get_free_list(bytes_left);
                ((obj *)(void *)start_mem_pool)->free_list_link = *my_free_list;
                *my_free_list = (obj *)(void *)start_mem_pool;
            }
            size_t bytes_to_get = 2 * total_bytes + round_up(heap_size >> 4); // bytes to be allocated from the heap
            try
            {
                start_mem_pool = static_cast<char *>(::operator new(bytes_to_get));
            }
            catch (const std::bad_alloc &)
            {
                // if there's not enough space in the heap to allocate, go to find the space already allocated in free_list yet not sent to the user
                for (size_t i = size; i <= (size_t)MAX_BYTES; i += (size_t)ALIGN)
                {
                    obj *volatile *my_free_list = get_free_list(i);
                    obj *p = *my_free_list;
                    if (p != nullptr)
                    {
                        *my_free_list = p->free_list_link;
                        start_mem_pool = (char *)p;
                        end_mem_pool = start_mem_pool + i;
                        return allocate_chunk(size, cnt_objs);
                    }
                }
                start_mem_pool = end_mem_pool = nullptr;
                throw;
            }
            // update and allocate again
            heap_size += bytes_to_get;
            end_mem_pool = start_mem_pool + bytes_to_get;
            return allocate_chunk(size, cnt_objs);
        }
    }
};

// initialize
char *pool_alloc_base ::start_mem_pool = nullptr;

char *pool_alloc_base ::end_mem_pool = nullptr;

std::size_t pool_alloc_base::heap_size = 0;

pool_alloc_base::obj *volatile pool_alloc_base::free_list[FREE_LIST_SIZE] = {nullptr};

template <class T>
class pool_alloc : public pool_alloc_base
{
public:
    typedef void _Not_user_specialized;
    typedef T value_type;
    typedef value_type *pointer;
    typedef const value_type *const_pointer;
    typedef value_type &reference;
    typedef const value_type &const_reference;
    typedef std::size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef std::true_type propagate_on_container_move_assignment;
    typedef std::true_type is_always_equal;

    pool_alloc() = default;

    template <class U>
    pool_alloc(const pool_alloc<U> &other) noexcept;

    pointer address(reference _Val) const noexcept { return &_Val; }

    const_pointer address(const_reference _Val) const noexcept { return &_Val; }

    pointer allocate(size_type _Count)
    {
        if (_Count > size_type(-1) / sizeof(value_type))
            throw std::bad_alloc();
        if (auto p = static_cast<pointer>(_allocate(_Count * sizeof(value_type))))
            return p;
        throw std::bad_alloc();
    }

    void deallocate(pointer _Ptr, size_type _Count)
    {
        _deallocate(_Ptr, _Count);
    }

    template <class _Uty>
    void destroy(_Uty *_Ptr)
    {
        _Ptr->~_Uty();
    }

    // Constructs an object of type T in allocated uninitialized storage pointed to by _Ptr, using placement-new
    template <class _Objty, class... _Types>
    void construct(_Objty *_Ptr, _Types &&..._Args)
    {
        ::new ((void *)_Ptr) _Objty(std::forward<_Types>(_Args)...);
    }

private:
    void *alloc(std::size_t n)
    {
        void *res = std::malloc(n);
        if (res == nullptr)
            throw std::bad_alloc();
        return res;
    }

    void dealloc(void *p, std::size_t) { std::free(p); }

    void *_allocate(size_type n)
    {
        obj *volatile *cur_free_list;
        obj *res;

        // if n is large enough to call the first allocator
        if (n > (size_type)MAX_BYTES)
            return alloc(n);

        // find the suitable free list
        cur_free_list = get_free_list(n);
        res = *cur_free_list;

        // refill the free list if there is no free list available
        if (res == nullptr)
        {
            void *r = refill(round_up(n));
            return r;
        }

        // adjust the free list
        *cur_free_list = res->free_list_link;
        return res;
    }

    void _deallocate(void *p, size_type n)
    {
        obj *q = (obj *)p;
        obj *volatile *cur_free_list;

        // if n is large enough, call the first deallocator
        if (n > (size_type)MAX_BYTES)
        {
            dealloc(p, n);
            return;
        }

        // find the suitable free list
        cur_free_list = get_free_list(n);

        // adjust the free list
        q->free_list_link = *cur_free_list;
        *cur_free_list = q;
    }
};
#endif