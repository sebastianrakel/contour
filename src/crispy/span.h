#pragma once
#include <iterator>
#include <type_traits>

namespace crispy
{

template <typename T>
class span
{
  public:
    using element_type = T;

    using pointer_type = element_type*;
    using const_pointer_type = element_type const*;

    using iterator = pointer_type;
    using const_iterator = pointer_type;

    using reference_type = element_type&;
    using const_reference_type = element_type const&;

    // constructors
    //
    constexpr span(iterator _begin, iterator _end) noexcept: begin_ { _begin }, end_ { _end } {}
    constexpr span(iterator _begin, size_t _count) noexcept:
        begin_ { _begin }, end_ { std::next(_begin, _count) }
    {
    }

    template <std::size_t N>
    constexpr span(element_type (&_array)[N]) noexcept: begin_ { _array }, end_ { _array + N }
    {
    }

    constexpr span() noexcept: begin_ {}, end_ {} {}
    constexpr span(span<T> const&) noexcept = default;
    constexpr span(span<T>&&) noexcept = default;
    constexpr span& operator=(span<T> const&) noexcept = default;
    constexpr span& operator=(span<T>&&) noexcept = default;

    // readonly properties
    //
    constexpr bool empty() const noexcept { return begin_ == end_; }
    constexpr size_t size() const noexcept { return static_cast<size_t>(std::distance(begin_, end_)); }

    // iterators
    //
    constexpr iterator begin() noexcept { return begin_; }
    constexpr iterator end() noexcept { return end_; }
    constexpr const_iterator begin() const noexcept { return begin_; }
    constexpr const_iterator end() const noexcept { return end_; }

    // random access
    //
    constexpr reference_type operator[](size_t i) noexcept { return begin_[i]; }
    constexpr const_reference_type operator[](size_t i) const noexcept { return begin_[i]; }

    constexpr reference_type front() const noexcept { return begin_[0]; }
    constexpr reference_type back() const noexcept { return std::prev(end_); }

    reference_type at(size_t i)
    {
        if (i >= size())
            throw std::invalid_argument("i");
        return begin_[i];
    }

    const_reference_type at(size_t i) const
    {
        if (i >= size())
            throw std::invalid_argument("i");
        return begin_[i];
    }

  private:
    pointer_type begin_;
    pointer_type end_;
};

template <typename T>
span(T, size_t) -> span<decltype(std::declval<T>().begin())>;

template <typename T>
constexpr bool operator==(span<T> const& a, span<T> const& b) noexcept
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i])
            return false;

    return true;
}

template <typename T>
constexpr bool operator!=(span<T> const& a, span<T> const& b) noexcept
{
    return !(a == b);
}

template <typename T>
constexpr auto begin(span<T>& _span) noexcept
{
    return _span.begin();
}
template <typename T>
constexpr auto end(span<T>& _span) noexcept
{
    return _span.end();
}

} // namespace crispy
