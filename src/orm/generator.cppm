///////////////////////////////////////////////////////////////////////////////
// Reference implementation of std::generator proposal P2168.
// Adapted for C++26 modules from lewissbaker/generator.
//
// See https://wg21.link/P2168 for details.
//
///////////////////////////////////////////////////////////////////////////////
// Copyright Lewis Baker, Corentin Jabot
//
// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0.
// (See accompanying file LICENSE or http://www.boost.org/LICENSE_1_0.txt)
///////////////////////////////////////////////////////////////////////////////

module;

#include <cassert>

export module storm_orm_generator;

import std;

export namespace storm {

    template <typename _T> class __manual_lifetime {
      public:
        __manual_lifetime() noexcept {}
        ~__manual_lifetime() {}

        template <typename... _Args>
        _T& construct(_Args&&... __args) noexcept(std::is_nothrow_constructible_v<_T, _Args...>) {
            return *::new (static_cast<void*>(std::addressof(__value_))) _T((_Args&&)__args...); // NOSONAR
        }

        void destruct() noexcept(std::is_nothrow_destructible_v<_T>) { // LCOV_EXCL_LINE
            __value_.~_T();                                            // LCOV_EXCL_LINE
        } // LCOV_EXCL_LINE

        _T& get() & noexcept { // LCOV_EXCL_LINE
            return __value_;   // LCOV_EXCL_LINE
        } // LCOV_EXCL_LINE
        _T&& get() && noexcept {
            return static_cast<_T&&>(__value_);
        }
        const _T& get() const& noexcept {
            return __value_;
        }
        const _T&& get() const&& noexcept {
            return static_cast<const _T&&>(__value_);
        }

      private:
        union {
            std::remove_const_t<_T> __value_;
        };
    };

    template <typename _T> class __manual_lifetime<_T&> {
      public:
        __manual_lifetime() noexcept : __value_(nullptr) {}
        ~__manual_lifetime() {}

        _T& construct(_T& __value) noexcept {
            __value_ = std::addressof(__value);
            return __value;
        }

        void destruct() noexcept {}

        _T& get() const noexcept {
            return *__value_;
        }

      private:
        _T* __value_;
    };

    template <typename _T> class __manual_lifetime<_T&&> {
      public:
        __manual_lifetime() noexcept : __value_(nullptr) {}
        ~__manual_lifetime() {}

        _T&& construct(_T&& __value) noexcept {
            __value_ = std::addressof(__value);
            return static_cast<_T&&>(__value);
        }

        void destruct() noexcept {}

        _T&& get() const noexcept {
            return static_cast<_T&&>(*__value_);
        }

      private:
        _T* __value_;
    };

    struct use_allocator_arg {};

    template <typename _Ref, typename _Value = std::remove_cvref_t<_Ref>, typename _Allocator = use_allocator_arg>
    class generator;

    template <typename _Ref> struct __generator_promise_base {
        template <typename _Ref2, typename _Value, typename _Alloc> friend class generator;

        __generator_promise_base*             __root_;
        std::coroutine_handle<>               __parentOrLeaf_;
        __manual_lifetime<std::exception_ptr> __exception_;
        __manual_lifetime<_Ref>               __value_;

        explicit __generator_promise_base(std::coroutine_handle<> thisCoro) noexcept
            : __root_(this), __parentOrLeaf_(thisCoro) {}

        ~__generator_promise_base() {
            if (__root_ != this) {       // LCOV_EXCL_LINE
                __exception_.destruct(); // LCOV_EXCL_LINE
            } // LCOV_EXCL_LINE
        }

        std::suspend_always initial_suspend() noexcept {
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception() {                           // LCOV_EXCL_LINE
            if (__root_ != this) {                             // LCOV_EXCL_LINE
                __exception_.get() = std::current_exception(); // LCOV_EXCL_LINE
            } else {                                           // LCOV_EXCL_LINE
                throw;                                         // LCOV_EXCL_LINE
            } // LCOV_EXCL_LINE
        } // LCOV_EXCL_LINE

        struct __final_awaiter {
            bool await_ready() noexcept {
                return false;
            }

            template <typename _Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<_Promise> __h) noexcept {
                _Promise&                 __promise = __h.promise();
                __generator_promise_base& __root    = *__promise.__root_;
                if (&__root != &__promise) {                            // LCOV_EXCL_LINE
                    auto __parent          = __promise.__parentOrLeaf_; // LCOV_EXCL_LINE
                    __root.__parentOrLeaf_ = __parent;                  // LCOV_EXCL_LINE
                    return __parent;                                    // LCOV_EXCL_LINE
                } // LCOV_EXCL_LINE
                return std::noop_coroutine();
            }

            void await_resume() noexcept {} // LCOV_EXCL_LINE
        };

        __final_awaiter final_suspend() noexcept {
            return {};
        }

        std::suspend_always yield_value(_Ref&& __x) noexcept(std::is_nothrow_move_constructible_v<_Ref>) {
            __root_->__value_.construct((_Ref&&)__x);
            return {};
        }

        template <typename _T>
            requires(!std::is_reference_v<_Ref>) && std::is_convertible_v<_T, _Ref>
        std::suspend_always yield_value(_T&& __x) noexcept(std::is_nothrow_constructible_v<_Ref, _T>) {
            __root_->__value_.construct((_T&&)__x);
            return {};
        }

        void resume() {
            __parentOrLeaf_.resume();
        }

        void await_transform() = delete;
    };

    template <typename _Generator, typename _ByteAllocator, bool _ExplicitAllocator = false> struct __generator_promise;

    template <typename _Ref, typename _Value, typename _Alloc, typename _ByteAllocator, bool _ExplicitAllocator>
    struct __generator_promise<generator<_Ref, _Value, _Alloc>, _ByteAllocator, _ExplicitAllocator> final
        : public __generator_promise_base<_Ref> {
        __generator_promise() noexcept
            : __generator_promise_base<_Ref>(std::coroutine_handle<__generator_promise>::from_promise(*this)) {}

        generator<_Ref, _Value, _Alloc> get_return_object() noexcept {
            return generator<_Ref, _Value, _Alloc>{std::coroutine_handle<__generator_promise>::from_promise(*this)};
        }

        using __generator_promise_base<_Ref>::yield_value;
    };

    // Type-erased allocator specialisation (default)
    template <typename _Ref, typename _Value>
    class generator<_Ref, _Value, use_allocator_arg> : public std::ranges::view_base {
        using __promise_base = __generator_promise_base<_Ref>;

      public:
        generator() noexcept : __promise_(nullptr), __coro_(), __started_(false) {}

        generator(generator&& __other) noexcept
            : __promise_(std::exchange(__other.__promise_, nullptr))
            , __coro_(std::exchange(__other.__coro_, {}))
            , __started_(std::exchange(__other.__started_, false)) {}

        ~generator() noexcept {
            if (__coro_) {
                if (__started_ && !__coro_.done()) {
                    __promise_->__value_.destruct();
                }
                __coro_.destroy();
            }
        }

        generator& operator=(generator g) noexcept {
            swap(g);
            return *this;
        }

        void swap(generator& __other) noexcept {
            std::swap(__promise_, __other.__promise_);
            std::swap(__coro_, __other.__coro_);
            std::swap(__started_, __other.__started_);
        }

        struct sentinel {};

        class iterator {
          public:
            using iterator_category = std::input_iterator_tag;
            using difference_type   = std::ptrdiff_t;
            using value_type        = _Value;
            using reference         = _Ref;
            using pointer           = std::add_pointer_t<_Ref>;

            iterator() noexcept       = default;
            iterator(const iterator&) = delete;

            iterator(iterator&& __other) noexcept
                : __promise_(std::exchange(__other.__promise_, nullptr)), __coro_(std::exchange(__other.__coro_, {})) {}

            iterator& operator=(iterator&& __other) {
                __promise_ = std::exchange(__other.__promise_, nullptr);
                __coro_    = std::exchange(__other.__coro_, {});
                return *this;
            }

            ~iterator() = default;

            friend bool operator==(const iterator& it, sentinel) noexcept {
                return it.__coro_.done();
            }

            iterator& operator++() {
                __promise_->__value_.destruct();
                __promise_->resume();
                return *this;
            }

            void operator++(int) {
                (void)operator++();
            }

            reference operator*() const noexcept {
                return static_cast<reference>(__promise_->__value_.get());
            }

          private:
            friend generator;

            explicit iterator(__promise_base* __promise, std::coroutine_handle<> __coro) noexcept
                : __promise_(__promise), __coro_(__coro) {}

            __promise_base*         __promise_;
            std::coroutine_handle<> __coro_;
        };

        iterator begin() {
            assert(__coro_);
            assert(!__started_);
            __started_ = true;
            __coro_.resume();
            return iterator{__promise_, __coro_};
        }

        sentinel end() noexcept {
            return {};
        }

      private:
        template <typename _Generator, typename _ByteAllocator, bool _ExplicitAllocator>
        friend struct __generator_promise;

        template <typename _Promise>
        explicit generator(std::coroutine_handle<_Promise> __coro) noexcept
            : __promise_(std::addressof(__coro.promise())), __coro_(__coro) {}

        __promise_base*         __promise_;
        std::coroutine_handle<> __coro_;
        bool                    __started_ = false;
    };

} // namespace storm

// Specialisations in namespace std — must be outside storm namespace
namespace std {

    template <typename _Ref, typename _Value, typename... _Args>
    struct coroutine_traits<storm::generator<_Ref, _Value>, _Args...> {
        using promise_type = storm::__generator_promise<storm::generator<_Ref, _Value>, std::allocator<std::byte>>;
    };

    namespace ranges {
        template <typename _T, typename _U, typename _Alloc>
        constexpr inline bool enable_view<storm::generator<_T, _U, _Alloc>> = true;
    } // namespace ranges

} // namespace std
