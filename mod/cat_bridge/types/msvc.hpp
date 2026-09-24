#pragma once

#include "utilities/memory.hpp"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <format>

// Layout descriptions of C++ standard library class implementations shipped with MSVC 2022.
//
// These descriptions allow host process objects to be manipulated without dependence
// on C++ stdlib implementation or C++ ABI used by the hook's native environment.
// ...
// "for reasons of sanitization", not to be mistaken for "for reasons of sanity"
//
// polymeric 2026

// MSVC XString (std::string), laid out as compiled in Release mode
// https://github.com/microsoft/STL/blob/26b7ca58ee3151c81736088e554e5797cafca641/stl/inc/xstring
struct MsvcReleaseModeXString {
    union {
        char _Buf[16];
        char *_Ptr;
    } _Bx;
    uint64_t _Mysize;
    uint64_t _Myres;

    // delete the copy constructor to block implicit copying
    MsvcReleaseModeXString(const MsvcReleaseModeXString&) = delete;
    MsvcReleaseModeXString& operator=(const MsvcReleaseModeXString&) = delete;

    const char *begin() const {
        if(this->_Myres < 16) {
            return &this->_Bx._Buf[0];
        } else {
            return this->_Bx._Ptr;
        }
    }

    const char *end() const {
        if(this->_Myres < 16) {
            return &this->_Bx._Buf[this->_Mysize];
        } else {
            return this->_Bx._Ptr + this->_Mysize;
        }
    }

    char *begin() {
        if(this->_Myres < 16) {
            return &this->_Bx._Buf[0];
        } else {
            return this->_Bx._Ptr;
        }
    }

    char *end() {
        if(this->_Myres < 16) {
            return &this->_Bx._Buf[this->_Mysize];
        } else {
            return this->_Bx._Ptr + this->_Mysize;
        }
    }

    std::string copy_to_native_string() const {
        return std::string(this->begin(), this->end());
    }

    std::string_view as_native_string_view() const {
        return std::string_view(this->begin(), this->_Mysize);
    }

    operator std::string() const {
        return this->copy_to_native_string();
    }

    operator std::string_view() const {
        return this->as_native_string_view();
    }

    MsvcReleaseModeXString() {}

    // Empty string
    void construct() {
        this->_Bx._Buf[0] = '\0';
        this->_Mysize = 0;
        this->_Myres = 15;
    }

    // Copy from sized buffer
    void construct(const char *data, size_t size) {
        if(size < 16) {
            std::memcpy(this->_Bx._Buf, data, size);
            this->_Bx._Buf[size] = '\0';
            this->_Mysize = size;
            this->_Myres = 15;
        } else {
            // + 1 for the nullterm
            this->_Bx._Ptr = static_cast<char *>(host_alloc(size + 1));
            std::memcpy(this->_Bx._Ptr, data, size);
            this->_Bx._Ptr[size] = '\0';
            // does not count the nullterm
            this->_Mysize = size;
            this->_Myres = size;
        }
    }

    // Copy from null-terminated string
    void construct(const char *c_str) {
        construct(c_str, std::strlen(c_str));
    }

    void destroy() {
        if(this->_Myres >= 16) {
            host_free(this->_Bx._Ptr);
        }
    }

    void resize(size_t size, const char fill) {
        if(size == this->_Mysize) {
            // no-op
            return;
        }
        else if(size < this->_Mysize) {
            // shrink, always in-place
            this->begin()[size] = '\0';
            this->_Mysize = size;
        } else if(/*size > this->_Mysize && */size <= this->_Myres) {
            // grow, but in-place within the existing reservation
            std::memset(this->begin() + this->_Mysize, fill, size - this->_Mysize);
            this->begin()[size] = '\0';
            this->_Mysize = size;
        } else /*if(size > this->_Mysize && size > this->_Myres)*/ {
            // grow, moving into a new heap allocation
            char *new_large_alloc = static_cast<char *>(host_alloc(size + 1));
            std::memcpy(new_large_alloc, this->begin(), this->_Mysize);
            std::memset(new_large_alloc + this->_Mysize, fill, size - this->_Mysize);
            new_large_alloc[size] = '\0';

            if(this->_Myres >= 16) {
                host_free(this->_Bx._Ptr);
            }

            this->_Bx._Ptr = new_large_alloc;
            this->_Myres = size;
            this->_Mysize = size;
        }
    }
};
template<>
struct std::formatter<MsvcReleaseModeXString> : std::formatter<std::string_view> {
    auto format(const MsvcReleaseModeXString &s, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(s, ctx);
    }
};

struct MsvcReleaseModeXWString {
    union {
        wchar_t _Buf[8];
        wchar_t *_Ptr;
    } _Bx;
    uint64_t _Mysize;
    uint64_t _Myres;

    // delete the copy constructor to block implicit copying
    MsvcReleaseModeXWString(const MsvcReleaseModeXWString&) = delete;
    MsvcReleaseModeXWString& operator=(const MsvcReleaseModeXWString&) = delete;

    const wchar_t *begin() const {
        if(this->_Myres < 8) {
            return &this->_Bx._Buf[0];
        } else {
            return this->_Bx._Ptr;
        }
    }

    const wchar_t *end() const {
        if(this->_Myres < 8) {
            return &this->_Bx._Buf[this->_Mysize];
        } else {
            return this->_Bx._Ptr + this->_Mysize;
        }
    }

    wchar_t *begin() {
        if(this->_Myres < 8) {
            return &this->_Bx._Buf[0];
        } else {
            return this->_Bx._Ptr;
        }
    }

    wchar_t *end() {
        if(this->_Myres < 8) {
            return &this->_Bx._Buf[this->_Mysize];
        } else {
            return this->_Bx._Ptr + this->_Mysize;
        }
    }

    std::wstring copy_to_native_wstring() const {
        return std::wstring(this->begin(), this->end());
    }

    std::wstring_view as_native_wstring_view() const {
        return std::wstring_view(this->begin(), this->_Mysize);
    }

    operator std::wstring() const {
        return this->copy_to_native_wstring();
    }

    operator std::wstring_view() const {
        return this->as_native_wstring_view();
    }

    MsvcReleaseModeXWString() {}

    // Empty string
    void construct() {
        this->_Bx._Buf[0] = L'\0';
        this->_Mysize = 0;
        this->_Myres = 7;
    }

    // Copy from sized buffer
    void construct(const wchar_t *data, size_t size) {
        if(size < 8) {
            std::wmemcpy(this->_Bx._Buf, data, size);
            this->_Bx._Buf[size] = L'\0';
            this->_Mysize = size;
            this->_Myres = 7;
        } else {
            // + 1 for the nullterm
            this->_Bx._Ptr = static_cast<wchar_t *>(host_alloc((size + 1) * sizeof(wchar_t)));
            std::wmemcpy(this->_Bx._Ptr, data, size);
            this->_Bx._Ptr[size] = L'\0';
            // does not count the nullterm
            this->_Mysize = size;
            this->_Myres = size;
        }
    }

    // Copy from null-terminated string
    void construct(const wchar_t *c_str) {
        construct(c_str, std::wcslen(c_str));
    }

    void destroy() {
        if(this->_Myres >= 8) {
            host_free(this->_Bx._Ptr);
        }
    }

    void resize(size_t size, const wchar_t fill) {
        if(size == this->_Mysize) {
            // no-op
            return;
        }
        else if(size < this->_Mysize) {
            // shrink, always in-place
            this->begin()[size] = '\0';
            this->_Mysize = size;
        } else if(/*size > this->_Mysize && */size <= this->_Myres) {
            // grow, but in-place within the existing reservation
            std::wmemset(this->begin() + this->_Mysize, fill, size - this->_Mysize);
            this->begin()[size] = '\0';
            this->_Mysize = size;
        } else /*if(size > this->_Mysize && size > this->_Myres)*/ {
            // grow, moving into a new heap allocation
            wchar_t *new_large_alloc = static_cast<wchar_t *>(host_alloc((size + 1) * sizeof(wchar_t)));
            std::wmemcpy(new_large_alloc, this->begin(), this->_Mysize);
            std::wmemset(new_large_alloc + this->_Mysize, fill, size - this->_Mysize);
            new_large_alloc[size] = '\0';

            if(this->_Myres >= 8) {
                host_free(this->_Bx._Ptr);
            }

            this->_Bx._Ptr = new_large_alloc;
            this->_Myres = size;
            this->_Mysize = size;
        }
    }
};
template<>
struct std::formatter<MsvcReleaseModeXWString> : std::formatter<std::wstring_view, wchar_t> {
    auto format(const MsvcReleaseModeXWString &s, std::wformat_context& ctx) const {
        return std::formatter<std::wstring_view, wchar_t>::format(s, ctx);
    }
};

// MSVC Func (std::function)
// Based off _Func_impl_no_alloc & friends
// https://github.com/microsoft/STL/blob/26b7ca58ee3151c81736088e554e5797cafca641/stl/inc/functional
template <class _Callable, class _Rx, class... _Types>
struct MsvcFuncNoAlloc_vtable;

template <class _Callable, class _Rx, class... _Types>
struct MsvcFuncNoAlloc;
template <class _Callable, class _Rx, class... _Types>
struct MsvcFuncNoAlloc<_Callable, _Rx(_Types...)> {
    using VTable = MsvcFuncNoAlloc_vtable<_Callable, _Rx(_Types...)>;

    VTable *vtable;
    union {
        _Callable _Callee;
        void *_Ptrs[7];
    } _Mystorage;

    // This struct definition by itself does not depend on the contents of _Callee.
    // Users of this struct may describe capture layout in _Callable or use an opaque arbitrary sized dummy type.
    // However, we do require _Mystorage to be exactly 7 qwords in length.
    // Since we don't model heap indirection (_Is_large==true), we place this assertion on sizeof(_Callable).
    static_assert(sizeof(_Callable) <= 7*8, "_Callable can't fit into the inline buffer. _Is_large==true is not supported.");

    MsvcFuncNoAlloc(VTable *vtable, _Callable callee) :
        vtable(vtable), _Mystorage(callee)
    {}

    // delete the copy constructor to block implicit copying
    MsvcFuncNoAlloc(const MsvcFuncNoAlloc&) = delete;
    MsvcFuncNoAlloc& operator=(const MsvcFuncNoAlloc&) = delete;
};
template <class _Callable, class _Rx, class... _Types>
struct std::formatter<MsvcFuncNoAlloc<_Callable, _Rx(_Types...)>> : std::formatter<std::string_view> {
    auto format(const MsvcFuncNoAlloc<_Callable, _Rx(_Types...)> &f, std::format_context& ctx) const {
        std::string s = std::format("(vtable: {:p}, fp: {:p}, _getimpl: {:p})",
            reinterpret_cast<void *>(f.vtable),
            f._Mystorage._Ptrs[0],
            f._Mystorage._Ptrs[6]
        );
        return std::formatter<std::string_view>::format(s, ctx);
    }
};

typedef void MsvcTypeInfo;

// vtable for superclass _Func_base
template <class _Callable, class _Rx, class... _Types>
struct MsvcFuncNoAlloc_vtable<_Callable, _Rx(_Types...)> {
    using Impl = MsvcFuncNoAlloc<_Callable, _Rx(_Types...)>;
    Impl* (__cdecl *_Copy)(Impl const* thiss, void* _Where);
    Impl* (__cdecl *_Move)(Impl* thiss, void* _Where);
    _Rx (__cdecl  *_Do_call)(Impl* thiss, _Types*... _Args);
    const MsvcTypeInfo* (__cdecl *_Target_type)(Impl const* thiss);
    void (__cdecl *_Delete_this)(Impl* thiss, bool _Dealloc);
    const void* (__cdecl *_Get)(Impl const* thiss);
};

// MSVC List (std::list), laid out as compiled in Release mode
// https://github.com/microsoft/STL/blob/2626cf1ee9d26be701b9bdbd2fb30db240456456/stl/inc/list
template<class _Value_type>
struct MsvcReleaseModeListNode {
    MsvcReleaseModeListNode *_Next; // successor node, or first element if head
    MsvcReleaseModeListNode *_Prev; // predecessor node, or last element if head
    _Value_type _Myval; // the stored value, unused if head

    // delete the copy constructor to block implicit copying
    MsvcReleaseModeListNode(const MsvcReleaseModeListNode&) = delete;
    MsvcReleaseModeListNode& operator=(const MsvcReleaseModeListNode&) = delete;
};

template<class _Value_type>
struct MsvcReleaseModeList {
    MsvcReleaseModeListNode<_Value_type> *_Myhead; // pointer to head node
    uint64_t _Mysize; // number of elements

    // delete the copy constructor to block implicit copying
    MsvcReleaseModeList(const MsvcReleaseModeList&) = delete;
    MsvcReleaseModeList& operator=(const MsvcReleaseModeList&) = delete;
};

// MSVC Vector (std::vector), laid out as compiled in Release mode
// https://github.com/microsoft/STL/blob/2626cf1ee9d26be701b9bdbd2fb30db240456456/stl/inc/vector
template<class _Value_type>
struct MsvcReleaseModeVector {
    _Value_type *_Myfirst;
    _Value_type *_Mylast;
    _Value_type *_Myend;

    // delete the copy constructor to block implicit copying
    MsvcReleaseModeVector(const MsvcReleaseModeVector&) = delete;
    MsvcReleaseModeVector& operator=(const MsvcReleaseModeVector&) = delete;

    const _Value_type *begin() const {
        return _Myfirst;
    }

    const _Value_type *end() const {
        return _Mylast;
    }

    _Value_type *begin() {
        return _Myfirst;
    }

    _Value_type *end() {
        return _Mylast;
    }

    size_t size() const {
        return this->_Mylast - this->_Myfirst;
    }

    size_t capacity() const {
        return this->_Myend - this->_Myfirst;
    }

    const _Value_type& operator[](size_t idx) const {
        return _Myfirst[idx];
    }

    _Value_type& operator[](size_t idx) {
        return _Myfirst[idx];
    }
};

// MSVC XHash (std::unordered_map etc.), laid out as compiled in Release mode
// https://github.com/microsoft/STL/blob/2626cf1ee9d26be701b9bdbd2fb30db240456456/stl/inc/xhash
template<class _Value_type>
struct MsvcReleaseModeXHashIterPair {
    _Value_type *first;
    _Value_type *last;
};
template<class _Value_type>
struct MsvcReleaseModeXHash {
     // Assume _Traitsobj only contains _Max_bucket_size for now.
    float _Max_bucket_size;
    uint32_t _Traitsobj_padding;
    MsvcReleaseModeList<_Value_type> _List;
    // actually a _Hash_vec, but is otherwise what we expect
    MsvcReleaseModeVector<MsvcReleaseModeXHashIterPair<_Value_type>> _Vec;
    // _Mask is always _Maxidx-1 to leverage power of two constraint
    size_t _Mask; // the key mask
    size_t _Maxidx; // current maximum key value, must be a power of 2

    // delete the copy constructor to block implicit copying
    MsvcReleaseModeXHash(const MsvcReleaseModeXHash&) = delete;
    MsvcReleaseModeXHash& operator=(const MsvcReleaseModeXHash&) = delete;
};
