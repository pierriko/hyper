#! /bin/sed -f

s/boost::tuples::null_type\(, \)\?//g
s/\(T[0-9]* = \)\?boost::detail::variant::void_\(, \)\?//g
s/mpl_::na\(, \)\?//g
s/boost::variant<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19>/boost::variant20/g 
