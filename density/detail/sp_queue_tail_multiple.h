
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016-2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once

namespace density
{
    namespace detail
    {
        template <typename BUSY_WAIT_FUNC>
            class SpinlockMutex : private BUSY_WAIT_FUNC
        {
        public:

            SpinlockMutex() noexcept
            {
                m_lock.clear();
            }

            SpinlockMutex(const BUSY_WAIT_FUNC & i_busy_wait)
                : BUSY_WAIT_FUNC(i_busy_wait)
            {
                m_lock.clear();
            }

            bool try_lock() noexcept
            {
                return !m_lock.test_and_set(mem_acquire);
            }

            void lock() noexcept
            {
                BUSY_WAIT_FUNC & busy_wait = *this;
                while (m_lock.test_and_set(mem_acquire))
                {
                    busy_wait();
                }
            }

            void unlock() noexcept
            {
                m_lock.clear(mem_release);
            }

            ~SpinlockMutex()
            {
                DENSITY_ASSERT_INTERNAL(m_lock.test_and_set() == false);
            }

        private:
            std::atomic_flag m_lock;
        };

        /** \internal Class template that implements put operations for spin-locking queues */
        template < typename COMMON_TYPE, typename RUNTIME_TYPE, typename ALLOCATOR_TYPE, typename BUSY_WAIT_FUNC>
            class SpQueue_TailMultiple : protected LFQueue_Base<COMMON_TYPE, RUNTIME_TYPE, ALLOCATOR_TYPE>
        {
        protected:

            using Base = LFQueue_Base<COMMON_TYPE, RUNTIME_TYPE, ALLOCATOR_TYPE>;
            using Base::ControlBlock;
            using Base::Block;
            using Base::same_page;
            using Base::min_alignment;
            using Base::s_alloc_granularity;
            using Base::s_type_offset;
            using Base::s_element_min_offset;
            using Base::s_rawblock_min_offset;
            using Base::s_end_control_offset;
            using Base::s_max_size_inpage;
            using Base::s_invalid_control_block;

            /** Whether the head should zero the content of pages before deallocating. */
            constexpr static bool s_deallocate_zeroed_pages = false;

            /** Type-safe (at least for the caller) version of s_invalid_control_block */
            static ControlBlock * invalid_control_block() noexcept
            {
                return reinterpret_cast<ControlBlock*>(s_invalid_control_block);
            }

            SpQueue_TailMultiple() noexcept
                : m_tail(invalid_control_block()),
                  m_initial_page(nullptr)
            {
            }

            SpQueue_TailMultiple(ALLOCATOR_TYPE && i_allocator) noexcept
                : Base(std::move(i_allocator)),
                  m_tail(invalid_control_block()),
                  m_initial_page(nullptr)
            {
            }

            SpQueue_TailMultiple(const ALLOCATOR_TYPE & i_allocator)
                : Base(i_allocator),
                  m_tail(invalid_control_block()),
                  m_initial_page(nullptr)
            {
            }

            SpQueue_TailMultiple(SpQueue_TailMultiple && i_source) noexcept
                : SpQueue_TailMultiple()
            {
                swap(i_source);
            }

            SpQueue_TailMultiple & operator = (SpQueue_TailMultiple && i_source) noexcept
            {
                SpQueue_TailMultiple::swap(i_source);
                return *this;
            }

            void swap(SpQueue_TailMultiple & i_other) noexcept
            {
                // swap the allocator
                using std::swap;
                swap(static_cast<ALLOCATOR_TYPE&>(*this), static_cast<ALLOCATOR_TYPE&>(i_other));

                // swap m_tail
                swap(m_tail, i_other.m_tail);

                // swap m_initial_page
                auto const tmp1 = i_other.m_initial_page.load();
                i_other.m_initial_page.store(m_initial_page.load());
                m_initial_page.store(tmp1);
            }

            ~SpQueue_TailMultiple()
            {
                if (m_tail != invalid_control_block())
                {
                    ALLOCATOR_TYPE::deallocate_page(m_tail);
                }
            }

            /** Given an address, returns the end block of the page containing it. */
            static ControlBlock * get_end_control_block(void * i_address) noexcept
            {
                auto const page = address_lower_align(i_address, ALLOCATOR_TYPE::page_alignment);
                return static_cast<ControlBlock *>(address_add(page, s_end_control_offset));
            }

            Block inplace_allocate(uintptr_t i_control_bits, bool i_include_type, size_t i_size, size_t i_alignment)
            {
                return try_inplace_allocate_impl<LfQueue_Throwing>(i_control_bits, i_include_type, i_size, i_alignment);
            }

            template <uintptr_t CONTROL_BITS, bool INCLUDE_TYPE, size_t SIZE, size_t ALIGNMENT>
                Block inplace_allocate()
            {
                return try_inplace_allocate_impl<LfQueue_Throwing, CONTROL_BITS, INCLUDE_TYPE, SIZE, ALIGNMENT>();
            }

            Block try_inplace_allocate(progress_guarantee i_progress_guarantee, uintptr_t i_control_bits, bool i_include_type, size_t i_size, size_t i_alignment) noexcept
            {
                switch (i_progress_guarantee)
                {
                case progress_wait_free:
                    return try_inplace_allocate_impl<LfQueue_WaitFree>(i_control_bits, i_include_type, i_size, i_alignment);
                case progress_lock_free:
                case progress_obstruction_free:
                    return try_inplace_allocate_impl<LfQueue_LockFree>(i_control_bits, i_include_type, i_size, i_alignment);
                default:
                    DENSITY_ASSERT_INTERNAL(false);
                case progress_blocking:
                    return try_inplace_allocate_impl<LfQueue_Blocking>(i_control_bits, i_include_type, i_size, i_alignment);
                }
            }

            /** Overload of inplace_allocate that can be used when all parameters are compile time constants */
            template <uintptr_t CONTROL_BITS, bool INCLUDE_TYPE, size_t SIZE, size_t ALIGNMENT>
                Block try_inplace_allocate(progress_guarantee i_progress_guarantee) noexcept
            {
                switch (i_progress_guarantee)
                {
                case progress_wait_free:
                    return try_inplace_allocate_impl<LfQueue_WaitFree, CONTROL_BITS, INCLUDE_TYPE, SIZE, ALIGNMENT>();
                case progress_lock_free:
                case progress_obstruction_free:
                    return try_inplace_allocate_impl<LfQueue_LockFree, CONTROL_BITS, INCLUDE_TYPE, SIZE, ALIGNMENT>();
                default:
                    DENSITY_ASSERT_INTERNAL(false);
                case progress_blocking:
                    return try_inplace_allocate_impl<LfQueue_Blocking, CONTROL_BITS, INCLUDE_TYPE, SIZE, ALIGNMENT>();
                }
            }

            static void commit_put_impl(const Block & i_put) noexcept
            {
                // we expect to have NbQueue_Busy and not NbQueue_Dead
                DENSITY_ASSERT_INTERNAL(address_is_aligned(i_put.m_control_block, s_alloc_granularity));
                DENSITY_ASSERT_INTERNAL(
                    (i_put.m_next_ptr & ~detail::NbQueue_AllFlags) == (raw_atomic_load(&i_put.m_control_block->m_next, detail::mem_relaxed) & ~detail::NbQueue_AllFlags) &&
                    (i_put.m_next_ptr & (detail::NbQueue_Busy | detail::NbQueue_Dead)) == detail::NbQueue_Busy);

                // remove the flag NbQueue_Busy
                raw_atomic_store(&i_put.m_control_block->m_next, i_put.m_next_ptr - detail::NbQueue_Busy, detail::mem_seq_cst);
            }

            static void cancel_put_impl(const Block & i_put) noexcept
            {
                // destroy the element and the type
                auto type_ptr = type_after_control(i_put.m_control_block);
                type_ptr->destroy(static_cast<COMMON_TYPE*>(i_put.m_user_storage));
                type_ptr->RUNTIME_TYPE::~RUNTIME_TYPE();

                cancel_put_nodestroy_impl(i_put);
            }

            static void cancel_put_nodestroy_impl(const Block & i_put) noexcept
            {
                // we expect to have NbQueue_Busy and not NbQueue_Dead
                DENSITY_ASSERT_INTERNAL(address_is_aligned(i_put.m_control_block, s_alloc_granularity));
                DENSITY_ASSERT_INTERNAL(
                    (i_put.m_next_ptr & ~detail::NbQueue_AllFlags) == (raw_atomic_load(&i_put.m_control_block->m_next, detail::mem_relaxed) & ~detail::NbQueue_AllFlags) &&
                    (i_put.m_next_ptr & (detail::NbQueue_Busy | detail::NbQueue_Dead)) == detail::NbQueue_Busy);

                // remove NbQueue_Busy and add NbQueue_Dead
                auto const addend = static_cast<uintptr_t>(detail::NbQueue_Dead) - static_cast<uintptr_t>(detail::NbQueue_Busy);
                raw_atomic_store(&i_put.m_control_block->m_next, i_put.m_next_ptr + addend, detail::mem_seq_cst);
            }

            ControlBlock * get_initial_page() const noexcept
            {
                return m_initial_page.load();
            }

            static RUNTIME_TYPE * type_after_control(ControlBlock * i_control) noexcept
            {
                return static_cast<RUNTIME_TYPE*>(address_add(i_control, s_type_offset));
            }

            static void * get_unaligned_element(ControlBlock * i_control, bool i_is_external) noexcept
            {
                auto result = address_add(i_control, s_element_min_offset);
                if (i_is_external)
                {
                    /* i_control and s_element_min_offset are aligned to alignof(ExternalBlock), so
                        we don't need to align further */
                    result = static_cast<ExternalBlock*>(result)->m_block;
                }
                return result;
            }

            static void * get_element(detail::LfQueueControl<void> * i_control, bool i_is_external)
            {
                auto result = address_add(i_control, s_element_min_offset);
                if (i_is_external)
                {
                    /* i_control and s_element_min_offset are aligned to alignof(ExternalBlock), so
                        we don't need to align further */
                    result = static_cast<ExternalBlock*>(result)->m_block;
                }
                else
                {
                    result = address_upper_align(result, type_after_control(i_control)->alignment());
                }
                return result;
            }

            template <typename TYPE>
                static TYPE * get_element(detail::LfQueueControl<TYPE> * i_control, bool /*i_is_external*/)
            {
                return i_control->m_element;
            }

        private:

            /** Allocates a block of memory.
                The block may be allocated in the pages or in a legacy memory block, depending on the size and the alignment.
                @param i_control_bits flags to add to the control block. Only NbQueue_Busy, NbQueue_Dead and NbQueue_External are supported
                @param i_include_type true if this is an element value, false if it's a raw allocation
                @param i_size it must be > 0 and a multiple of the alignment
                @param i_alignment is must be > 0 and a power of two */
            template<LfQueue_ProgressGuarantee PROGRESS_GUARANTEE>
                Block try_inplace_allocate_impl(uintptr_t i_control_bits, bool i_include_type, size_t i_size, size_t i_alignment)
                    noexcept(PROGRESS_GUARANTEE != LfQueue_Throwing)
            {
                auto guarantee = PROGRESS_GUARANTEE; // used to avoid warnings about constant conditional expressions

                DENSITY_ASSERT_INTERNAL((i_control_bits & ~(detail::NbQueue_Busy | detail::NbQueue_Dead | detail::NbQueue_External)) == 0);
                DENSITY_ASSERT_INTERNAL(is_power_of_2(i_alignment) && (i_size % i_alignment) == 0);

                if (i_alignment < min_alignment)
                {
                    i_alignment = min_alignment;
                    i_size = uint_upper_align(i_size, min_alignment);
                }


                std::unique_lock<decltype(m_mutex)> lock(m_mutex, std::defer_lock);
                if (guarantee == LfQueue_Throwing || guarantee == LfQueue_Blocking)
                {
                    lock.lock();
                }
                else
                {
                    if (!lock.try_lock())
                        return Block{};
                }

                auto tail = m_tail;
                for (;;)
                {
                    DENSITY_ASSERT_INTERNAL(tail != nullptr && address_is_aligned(tail, s_alloc_granularity));

                    // allocate space for the control block (and possibly the runtime type)
                    void * address = address_add(tail, i_include_type ? s_element_min_offset : s_rawblock_min_offset);

                    // allocate space for the element
                    address = address_upper_align(address, i_alignment);
                    void * const user_storage = address;
                    address = address_add(address, i_size);
                    address = address_upper_align(address, s_alloc_granularity);
                    auto const new_tail = static_cast<ControlBlock*>(address);

                    // check for page overflow
                    auto const new_tail_offset = address_diff(new_tail, address_lower_align(tail, ALLOCATOR_TYPE::page_alignment));
                    if (DENSITY_LIKELY(new_tail_offset <= s_end_control_offset))
                    {
                        /* note: while control_block->m_next is zero, no consumers may ever read this
                            variable. So this does not need to be atomic store. */
                        //new_tail->m_next = 0;
                        /* edit: clang5 thread sanitizer has reported a data race between this write and the read:
                            auto const next_uint = raw_atomic_load(&control->m_next, detail::mem_relaxed);
                            in start_consume_impl (detail\lf_queue_head_multiple.h).
                            Making the store atomic.... */
                        raw_atomic_store(&new_tail->m_next, uintptr_t(0));

                        auto const control_block = tail;
                        auto const next_ptr = reinterpret_cast<uintptr_t>(new_tail) + i_control_bits;
                        DENSITY_ASSERT_INTERNAL(raw_atomic_load(&control_block->m_next, detail::mem_relaxed) == 0);
                        raw_atomic_store(&control_block->m_next, next_ptr, detail::mem_release);

                        DENSITY_ASSERT_INTERNAL(control_block < get_end_control_block(tail));
                        m_tail = new_tail;
                        return { control_block, next_ptr, user_storage };
                    }
                    else if (i_size + (i_alignment - min_alignment) <= s_max_size_inpage) // if this allocation may fit in a page
                    {
                        tail = page_overflow(PROGRESS_GUARANTEE, tail);
                        if (guarantee != LfQueue_Throwing)
                        {
                            if (tail == 0)
                            {
                                return Block();
                            }
                        }
                        else
                        {
                            DENSITY_ASSERT_INTERNAL(tail != 0);
                        }
                        m_tail = tail;
                    }
                    else
                    {
                        // this allocation would never fit in a page, allocate an external block
                        lock.unlock(); // this avoids recursive locks
                        return external_allocate<PROGRESS_GUARANTEE>(i_control_bits, i_size, i_alignment);
                    }
                }
            }

            /** Overload of try_inplace_allocate_impl that can be used when all parameters are compile time constants */
            template <LfQueue_ProgressGuarantee PROGRESS_GUARANTEE, uintptr_t CONTROL_BITS, bool INCLUDE_TYPE, size_t SIZE, size_t ALIGNMENT>
                Block try_inplace_allocate_impl()
                    noexcept(PROGRESS_GUARANTEE != LfQueue_Throwing)
            {
                auto guarantee = PROGRESS_GUARANTEE; // used to avoid warnings about constant conditional expressions

                static_assert((CONTROL_BITS & ~(detail::NbQueue_Busy | detail::NbQueue_Dead | detail::NbQueue_External)) == 0, "");
                static_assert(is_power_of_2(ALIGNMENT) && (SIZE % ALIGNMENT) == 0, "");

                constexpr auto alignment = detail::size_max(ALIGNMENT, min_alignment);
                constexpr auto size = uint_upper_align(SIZE, alignment);
                constexpr auto can_fit_in_a_page = size + (alignment - min_alignment) <= s_max_size_inpage;
                constexpr auto over_aligned = alignment > min_alignment;

                std::unique_lock<decltype(m_mutex)> lock(m_mutex, std::defer_lock);
                if (guarantee == LfQueue_Throwing || guarantee == LfQueue_Blocking)
                {
                    lock.lock();
                }
                else
                {
                    if (!lock.try_lock())
                        return Block{};
                }

                auto tail = m_tail;
                for (;;)
                {
                    DENSITY_ASSERT_INTERNAL(tail != nullptr && address_is_aligned(tail, s_alloc_granularity));

                    // allocate space for the control block (and possibly the runtime type)
                    void * address = address_add(tail, INCLUDE_TYPE ? s_element_min_offset : s_rawblock_min_offset);

                    // allocate space for the element
                    if (over_aligned)
                    {
                        address = address_upper_align(address, alignment);
                    }
                    void * const user_storage = address;
                    address = address_add(address, size);
                    address = address_upper_align(address, s_alloc_granularity);
                    auto const new_tail = static_cast<ControlBlock*>(address);

                    // check for page overflow
                    auto const new_tail_offset = address_diff(new_tail, address_lower_align(tail, ALLOCATOR_TYPE::page_alignment));
                    if (DENSITY_LIKELY(new_tail_offset <= s_end_control_offset))
                    {
                        /* note: while control_block->m_next is zero, no consumers may ever read this
                            variable. So this does not need to be atomic store. */
                        //new_tail->m_next = 0;
                            /* edit: clang5 thread sanitizer has reported a data race between this write and the read:
                            auto const next_uint = raw_atomic_load(&control->m_next, detail::mem_relaxed);
                            in start_consume_impl (detail\lf_queue_head_multiple.h).
                            Making the store atomic.... */
                        raw_atomic_store(&new_tail->m_next, uintptr_t(0));

                        auto const control_block = tail;
                        auto const next_ptr = reinterpret_cast<uintptr_t>(new_tail) + CONTROL_BITS;
                        DENSITY_ASSERT_INTERNAL(raw_atomic_load(&control_block->m_next, detail::mem_relaxed) == 0);
                        raw_atomic_store(&control_block->m_next, next_ptr, detail::mem_release);

                        DENSITY_ASSERT_INTERNAL(control_block < get_end_control_block(tail));
                        DENSITY_ASSERT_INTERNAL(new_tail != nullptr);
                        m_tail = new_tail;

                        return Block{ control_block, next_ptr, user_storage };
                    }
                    else if (can_fit_in_a_page) // if this allocation may fit in a page
                    {
                        tail = page_overflow(PROGRESS_GUARANTEE, tail);
                        if (guarantee != LfQueue_Throwing)
                        {
                            if (tail == 0)
                            {
                                return Block();
                            }
                        }
                        else
                        {
                            DENSITY_ASSERT_INTERNAL(tail != 0);
                        }
                        m_tail = tail;
                    }
                    else
                    {
                        // this allocation would never fit in a page, allocate an external block
                        lock.unlock(); // this avoids recursive locks
                        return external_allocate<PROGRESS_GUARANTEE>(CONTROL_BITS, SIZE, ALIGNMENT);
                    }
                }
            }

               /** Used by inplace_allocate when the block can't be allocated in a page. */
            template <LfQueue_ProgressGuarantee PROGRESS_GUARANTEE>
                Block external_allocate(uintptr_t i_control_bits, size_t i_size, size_t i_alignment)
                    noexcept(PROGRESS_GUARANTEE != LfQueue_Throwing)
            {
                auto guarantee = PROGRESS_GUARANTEE; // used to avoid warnings about constant conditional expressions

                void * external_block;
                if (guarantee == LfQueue_Throwing)
                {
                    external_block = ALLOCATOR_TYPE::allocate(i_size, i_alignment);
                }
                else
                {
                    external_block = ALLOCATOR_TYPE::try_allocate(ToDenGuarantee(PROGRESS_GUARANTEE), i_size, i_alignment);
                    if (external_block == nullptr)
                    {
                        return Block();
                    }
                }

                try
                {
                    /* external blocks always allocate space for the type, because it would be complicated
                        for the consumers to handle both cases*/
                    auto const inplace_put = try_inplace_allocate_impl<PROGRESS_GUARANTEE>(i_control_bits | detail::NbQueue_External, true, sizeof(ExternalBlock), alignof(ExternalBlock));
                    if (inplace_put.m_user_storage == nullptr)
                    {
                        ALLOCATOR_TYPE::deallocate(external_block, i_size, i_alignment);
                        return Block();
                    }
                    new(inplace_put.m_user_storage) ExternalBlock{external_block, i_size, i_alignment};
                    return Block{ inplace_put.m_control_block, inplace_put.m_next_ptr, external_block };
                }
                catch (...)
                {
                    /* if inplace_allocate fails, that means that we were able to allocate the external block,
                        but we were not able to put the struct ExternalBlock in the page (because a new page was
                        necessary, but we could not allocate it). */
                    ALLOCATOR_TYPE::deallocate(external_block, i_size, i_alignment);
                    DENSITY_INTERNAL_RETHROW_WITHIN_POSSIBLY_NOEXCEPT
                }
            }

            /** Handles a page overflow of the tail. This function may allocate a new page.
                @param i_progress_guarantee progress guarantee. If the function can't provide this guarantee, the function fails
                @param i_tail the value read from m_tail. Note that other threads may have updated m_tail
                    in then meanwhile.
                @return the new tail, or nullptr in case of failure. */
            DENSITY_NO_INLINE ControlBlock * page_overflow(LfQueue_ProgressGuarantee i_progress_guarantee, ControlBlock * const i_tail)
            {
                auto const page_end = get_end_control_block(i_tail);
                if (i_tail < page_end)
                {
                    /* There is space between the (presumed) current tail and the end control block.
                        We try to pad it with a dead element. */

                    DENSITY_ASSERT_INTERNAL(m_tail == i_tail);
                    m_tail = i_tail;

                    auto const block = static_cast<ControlBlock*>(i_tail);
                    raw_atomic_store(&block->m_next, reinterpret_cast<uintptr_t>(page_end) + detail::NbQueue_Dead, detail::mem_release);
                    return page_end;
                }
                else
                {
                    // get or allocate a new page
                    DENSITY_ASSERT_INTERNAL(i_tail == page_end);
                    return get_or_allocate_next_page(i_progress_guarantee, i_tail);
                }
            }


            /** Tries to allocate a new page. Returns the new value of m_tail.
                @param i_progress_guarantee progress guarantee. If the function can't provide this guarantee, the function returns nullptr
                @param i_tail the value read from m_tail.
                @return an updated value of tail, that makes the current thread progress. */
            ControlBlock * get_or_allocate_next_page(LfQueue_ProgressGuarantee i_progress_guarantee, ControlBlock * const i_end_control)
            {
                DENSITY_ASSERT_INTERNAL(i_end_control != nullptr &&
                    address_is_aligned(i_end_control, s_alloc_granularity) &&
                    i_end_control == get_end_control_block(i_end_control));

                if (i_end_control != invalid_control_block())
                {
                    // allocate and setup a new page
                    auto new_page = create_page(i_progress_guarantee);
                    if (new_page == nullptr)
                    {
                        return nullptr;
                    }

                    raw_atomic_store(&i_end_control->m_next, reinterpret_cast<uintptr_t>(new_page) + detail::NbQueue_Dead);

                    m_tail = new_page;

                    return m_tail;
                }
                else
                {
                    return create_initial_page(i_progress_guarantee);
                }
            }

            ControlBlock * create_initial_page(LfQueue_ProgressGuarantee i_progress_guarantee)
            {
                // m_initial_page = initial_page = create_page()
                auto const initial_page = create_page(i_progress_guarantee);
                if (initial_page == nullptr)
                {
                    return nullptr;
                }
                DENSITY_ASSERT_INTERNAL(m_initial_page.load() == nullptr);
                m_initial_page.store(initial_page);

                // m_tail = initial_page;
                DENSITY_ASSERT_INTERNAL(m_tail == invalid_control_block());
                m_tail = initial_page;

                return m_tail;
            }

            ControlBlock * create_page(LfQueue_ProgressGuarantee i_progress_guarantee)
            {
                auto const new_page = static_cast<ControlBlock *>(
                    i_progress_guarantee == LfQueue_Throwing ? ALLOCATOR_TYPE::allocate_page() :
                    ALLOCATOR_TYPE::try_allocate_page(ToDenGuarantee(i_progress_guarantee)) );
                if (new_page)
                {
                    auto const new_page_end_block = get_end_control_block(new_page);
                    raw_atomic_store(&new_page_end_block->m_next, uintptr_t(detail::NbQueue_InvalidNextPage));

                    raw_atomic_store(&new_page->m_next, uintptr_t(0), mem_release);
                }
                return new_page;
            }

        private: // data members
            alignas(concurrent_alignment) SpinlockMutex<BUSY_WAIT_FUNC> m_mutex;
            ControlBlock * m_tail;
            std::atomic<ControlBlock*> m_initial_page;
        };

    } // namespace detail

} // namespace density
