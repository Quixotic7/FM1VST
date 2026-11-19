// SPDX-License-Identifier: GPL-3.0-only
// The JamesOS core's one piece of the C++ library: libc++ declares std::sort's float instantiation extern (its
// __algorithm/sort.h; the definition is in the libc++ library's algorithm.cpp), and Plaits' chord_bank.cc and
// chiptune_engine.cc sort floats. The .rfm1 links no libc++ library, so the template is defined here (a heap sort
// from the header's own make_heap / sort_heap: the same order for floats) and that one instantiation made (weak_odr:
// a build that also links libc++'s copy keeps one).
#include <algorithm>
#if defined(_LIBCPP_VERSION)
_LIBCPP_BEGIN_NAMESPACE_STD
template <class _Comp, class _RandomAccessIterator>
void __sort(_RandomAccessIterator __first, _RandomAccessIterator __last, _Comp)
{
    // (the comparator is libc++'s __less, an ascending order: std::less of the element type is the same)
    typedef typename iterator_traits<_RandomAccessIterator>::value_type _Tp;
    std::make_heap(__first, __last, std::less<_Tp>());
    std::sort_heap(__first, __last, std::less<_Tp>());
}
template void __sort<__less<float>&, float*>(float*, float*, __less<float>&);
_LIBCPP_END_NAMESPACE_STD
#endif
