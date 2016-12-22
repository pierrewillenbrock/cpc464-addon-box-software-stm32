
/* Define to omit deprecated API from the library. */
/*#undef SIGCXX_DISABLE_DEPRECATED*/

/* Major version number of sigc++. */
#define SIGCXX_MAJOR_VERSION 2

/* Micro version number of sigc++. */
#define SIGCXX_MICRO_VERSION 0

/* Minor version number of sigc++. */
#define SIGCXX_MINOR_VERSION 10

# define SIGC_CONFIGURE 1

/* does the C++ compiler support the use of a particular specialization when
   calling operator() template methods. */
# define SIGC_GCC_TEMPLATE_SPECIALIZATION_OPERATOR_OVERLOAD 1

/* Define if the non-standard Sun reverse_iterator must be used. */
/*# undef SIGC_HAVE_SUN_REVERSE_ITERATOR*/

/* does the C++ compiler support the use of a particular specialization when
   calling operator() template methods omitting the template keyword. */
# define SIGC_MSVC_TEMPLATE_SPECIALIZATION_OPERATOR_OVERLOAD 1

/* does the C++ preprocessor support pragma push_macro() and pop_macro(). */
# define SIGC_PRAGMA_PUSH_POP_MACRO 1

# define SIGC_API
